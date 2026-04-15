#include "host_comm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include <stdarg.h>
#include <sys/time.h>

/* ─── Internal types ────────────────────────────────────────────────────────── */

typedef struct {
    host_resp_cb_t cb;
    void          *userdata;
} pending_t;

#define PENDING_MAX 64

typedef struct {
    int    cmd_fd;
    int    fb_fd;
    bool   connected;

    /* Command socket mutex + pending response queue */
    pthread_mutex_t cmd_mutex;
    pending_t       pending[PENDING_MAX];
    int             pending_head;
    int             pending_tail;

    /* Feedback thread */
    pthread_t          fb_thread;
    bool               fb_running;
    host_feedback_cb_t feedback_cb;
    void              *feedback_ud;

    /* Response receive buffer */
    char resp_buf[HOST_RESP_MAX];
    int  resp_len;
} host_state_t;

static host_state_t g_host = {
    .cmd_fd    = -1,
    .fb_fd     = -1,
    .connected = false,
};

/* ─── Helpers ───────────────────────────────────────────────────────────────── */

static int tcp_connect(const char *addr, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    /* RST on close — prevents mod-host from getting stuck in CLOSE_WAIT when
     * we exit or crash, which would block the next connection attempt */
    struct linger lg = { .l_onoff = 1, .l_linger = 0 };
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port   = htons((uint16_t)port),
    };
    if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) { close(fd); return -1; }

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
    return fd;
}

/* Parse "resp <status> [value]" — returns status, copies value into val_buf */
static int parse_response(const char *line, char *val_buf, size_t val_sz)
{
    int status = -999;
    if (sscanf(line, "resp %d", &status) < 1) {
        fprintf(stderr, "[host_comm] Unexpected response: %s\n", line);
        return -1;
    }
    if (val_buf && val_sz > 0) {
        val_buf[0] = '\0';
        /* Value starts after "resp <num> " */
        const char *p = line;
        /* Skip "resp " */
        while (*p && *p != ' ') p++;
        if (*p) p++;
        /* Skip status number */
        while (*p && *p != ' ') p++;
        if (*p) p++;
        /* Rest is value */
        snprintf(val_buf, val_sz, "%s", p);
        /* Strip trailing newline */
        size_t l = strlen(val_buf);
        while (l > 0 && (val_buf[l-1] == '\n' || val_buf[l-1] == '\r'))
            val_buf[--l] = '\0';
    }
    return status;
}

/* ─── Feedback thread ────────────────────────────────────────────────────────── */

static void *fb_thread_func(void *arg)
{
    host_state_t *h = (host_state_t *)arg;
    char buf[4096];
    int  buf_len = 0;

    while (h->fb_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(h->fb_fd, &rfds);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000}; /* 100ms */

        int r = select(h->fb_fd + 1, &rfds, NULL, NULL, &tv);
        if (r <= 0) continue;

        int n = recv(h->fb_fd, buf + buf_len, sizeof(buf) - buf_len - 1, 0);
        if (n <= 0) break;
        buf_len += n;
        buf[buf_len] = '\0';

        /* Process null-terminated messages.
         * Use strnlen to stay within received data — a message without a null
         * terminator would otherwise make strlen scan past buf_len. */
        char *p = buf;
        while (p < buf + buf_len) {
            size_t remaining = (size_t)(buf + buf_len - p);
            size_t mlen = strnlen(p, remaining);
            if (mlen == 0) { p++; continue; }
            if (mlen == remaining) {
                /* No null terminator yet — incomplete message; wait for more data */
                break;
            }
            if (h->feedback_cb)
                h->feedback_cb(p, h->feedback_ud);
            p += mlen + 1;
        }
        /* Shift remaining partial data to front of buffer */
        int leftover = (int)(buf + buf_len - p);
        if (leftover > 0) memmove(buf, p, (size_t)leftover);
        buf_len = leftover;
    }
    return NULL;
}

/* ─── Command receive (one response per command, called under cmd_mutex) ─────── */

static int recv_response(int fd, char *val_buf, size_t val_sz, int timeout_ms)
{
    char buf[HOST_RESP_MAX];
    int  buf_len = 0;

    struct timespec deadline;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec  += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        if (timeout_ms > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long ms_left = (deadline.tv_sec  - now.tv_sec) * 1000
                         + (deadline.tv_nsec - now.tv_nsec) / 1000000L;
            if (ms_left <= 0) return -ETIMEDOUT;
            tv.tv_sec  = ms_left / 1000;
            tv.tv_usec = (ms_left % 1000) * 1000;
        }

        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0)  return -errno;
        if (r == 0) continue; /* timeout_ms=0 means wait forever */

        int n = recv(fd, buf + buf_len, sizeof(buf) - buf_len - 1, 0);
        if (n <= 0) return -EIO;
        buf_len += n;
        buf[buf_len] = '\0';

        /* Look for a complete null-terminated response */
        char *p = buf;
        while (p < buf + buf_len) {
            size_t mlen = strlen(p);
            if (p + mlen < buf + buf_len) {
                /* Complete message */
                int status = parse_response(p, val_buf, val_sz);
                return status;
            }
            p += mlen;
            if (p < buf + buf_len) p++; /* skip null byte */
        }
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

int host_comm_connect(const char *addr, int cmd_port, int fb_port,
                      host_feedback_cb_t feedback_cb, void *feedback_ud)
{
    /* Retry loop — at boot, mod-host may not be listening yet even though
     * systemd reports it as "active".  Retry for up to 10 s (20 × 500 ms). */
    for (int attempt = 1; attempt <= 20; attempt++) {
        g_host.cmd_fd = tcp_connect(addr, cmd_port);
        if (g_host.cmd_fd >= 0) break;
        fprintf(stderr, "[host_comm] Connect attempt %d/20 to %s:%d failed — retrying in 500 ms\n",
                attempt, addr, cmd_port);
        usleep(500000);
    }
    if (g_host.cmd_fd < 0) {
        fprintf(stderr, "[host_comm] Cannot connect to %s:%d after 20 attempts: %s\n",
                addr, cmd_port, strerror(errno));
        return -1;
    }

    g_host.fb_fd = tcp_connect(addr, fb_port);
    if (g_host.fb_fd < 0) {
        fprintf(stderr, "[host_comm] Cannot connect feedback %s:%d: %s\n",
                addr, fb_port, strerror(errno));
        close(g_host.cmd_fd);
        g_host.cmd_fd = -1;
        return -1;
    }

    pthread_mutex_init(&g_host.cmd_mutex, NULL);
    g_host.feedback_cb  = feedback_cb;
    g_host.feedback_ud  = feedback_ud;
    g_host.fb_running   = true;
    g_host.connected    = true;
    g_host.pending_head = 0;
    g_host.pending_tail = 0;

    pthread_create(&g_host.fb_thread, NULL, fb_thread_func, &g_host);
    printf("[host_comm] Connected to mod-host %s cmd:%d fb:%d\n", addr, cmd_port, fb_port);
    return 0;
}

void host_comm_disconnect(void)
{
    if (!g_host.connected) return;
    g_host.fb_running = false;
    pthread_join(g_host.fb_thread, NULL);

    if (g_host.fb_fd  >= 0) { close(g_host.fb_fd);  g_host.fb_fd  = -1; }
    if (g_host.cmd_fd >= 0) { close(g_host.cmd_fd); g_host.cmd_fd = -1; }

    pthread_mutex_destroy(&g_host.cmd_mutex);
    g_host.connected = false;
}

bool host_comm_is_connected(void)
{
    return g_host.connected && g_host.cmd_fd >= 0;
}

int host_comm_send(const char *cmd, host_resp_cb_t cb, void *userdata)
{
    if (!host_comm_is_connected()) return -ENOTCONN;

    /* Append null byte terminator as per mod-host protocol */
    size_t len = strlen(cmd);
    char  *buf = malloc(len + 1);
    if (!buf) return -ENOMEM;
    memcpy(buf, cmd, len);
    buf[len] = '\0';

    pthread_mutex_lock(&g_host.cmd_mutex);

    /* Send command */
    ssize_t sent = send(g_host.cmd_fd, buf, len + 1, 0);
    free(buf);
    if (sent < 0) {
        pthread_mutex_unlock(&g_host.cmd_mutex);
        return -errno;
    }

    /* Receive response */
    char val[HOST_RESP_MAX];
    int status = recv_response(g_host.cmd_fd, val, sizeof(val), 5000);

    pthread_mutex_unlock(&g_host.cmd_mutex);

    if (cb) cb(status, val, userdata);
    return status;
}

int host_comm_send_sync(const char *cmd, char *val_buf, size_t val_bufsz, int timeout_ms)
{
    if (!host_comm_is_connected()) return -ENOTCONN;

    size_t len = strlen(cmd);
    char  *buf = malloc(len + 1);
    if (!buf) return -ENOMEM;
    memcpy(buf, cmd, len);
    buf[len] = '\0';

    pthread_mutex_lock(&g_host.cmd_mutex);
    ssize_t sent = send(g_host.cmd_fd, buf, len + 1, 0);
    free(buf);
    if (sent < 0) {
        pthread_mutex_unlock(&g_host.cmd_mutex);
        return -errno;
    }

    char val[HOST_RESP_MAX];
    int status = recv_response(g_host.cmd_fd, val,
                               val_buf ? val_bufsz : sizeof(val),
                               timeout_ms);
    if (val_buf && val_bufsz > 0)
        snprintf(val_buf, val_bufsz, "%s", val);

    pthread_mutex_unlock(&g_host.cmd_mutex);
    return status;
}

/* ─── High-level helpers ──────────────────────────────────────────────────────── */

int host_add_plugin(int instance, const char *uri)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "add %s %d", uri, instance);
    int r = host_comm_send_sync(cmd, NULL, 0, 30000);
    fprintf(stderr, "[host] add %d %s → %d\n", instance, uri, r);
    return r;
}

int host_remove_plugin(int instance)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "remove %d", instance);
    int r = host_comm_send_sync(cmd, NULL, 0, 5000);
    fprintf(stderr, "[host] remove %d → %d\n", instance, r);
    return r;
}

int host_bypass(int instance, bool bypass)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "bypass %d %d", instance, bypass ? 1 : 0);
    int r = host_comm_send_sync(cmd, NULL, 0, 2000);
    fprintf(stderr, "[host] bypass %d %d → %d\n", instance, bypass ? 1 : 0, r);
    return r;
}

int host_param_set(int instance, const char *symbol, float value)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "param_set %d %s %f", instance, symbol, (double)value);
    return host_comm_send_sync(cmd, NULL, 0, 2000);
}

int host_param_get(int instance, const char *symbol, float *out)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "param_get %d %s", instance, symbol);
    char val[64];
    int status = host_comm_send_sync(cmd, val, sizeof(val), 2000);
    if (status >= 0 && out) *out = (float)atof(val);
    return status;
}

int host_connect(const char *port_a, const char *port_b)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "connect %s %s", port_a, port_b);
    int r = host_comm_send_sync(cmd, NULL, 0, 3000);
    fprintf(stderr, "[host] connect %s %s → %d\n", port_a, port_b, r);
    return r;
}

int host_disconnect(const char *port_a, const char *port_b)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "disconnect %s %s", port_a, port_b);
    return host_comm_send_sync(cmd, NULL, 0, 3000);
}

int host_disconnect_all(const char *port)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "disconnect_all %s", port);
    return host_comm_send_sync(cmd, NULL, 0, 3000);
}

int host_remove_all(void)
{
    int r = host_comm_send_sync("remove -1", NULL, 0, 5000);
    fprintf(stderr, "[host] remove -1 (all) → %d\n", r);
    return r;
}

int host_state_load(const char *dir)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "state_load \"%s\"", dir);
    return host_comm_send_sync(cmd, NULL, 0, 10000);
}

int host_state_save(const char *dir)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "state_save \"%s\"", dir);
    return host_comm_send_sync(cmd, NULL, 0, 10000);
}

int host_preset_load(int instance, const char *preset_uri)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "preset_load %d \"%s\"", instance, preset_uri);
    return host_comm_send_sync(cmd, NULL, 0, 5000);
}

int host_preset_save(int instance, const char *name, const char *dir, const char *filename)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "preset_save %d \"%s\" \"%s\" \"%s\"",
             instance, name, dir, filename);
    return host_comm_send_sync(cmd, NULL, 0, 5000);
}

int host_midi_map(int instance, const char *symbol, int channel, int cc,
                  float min, float max)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "midi_map %d %s %d %d %f %f",
             instance, symbol, channel, cc, (double)min, (double)max);
    return host_comm_send_sync(cmd, NULL, 0, 2000);
}

int host_midi_unmap(int instance, const char *symbol)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "midi_unmap %d %s", instance, symbol);
    return host_comm_send_sync(cmd, NULL, 0, 2000);
}

int host_transport(bool rolling, float bpb, float bpm)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "transport %d %f %f",
             rolling ? 1 : 0, (double)bpb, (double)bpm);
    return host_comm_send_sync(cmd, NULL, 0, 2000);
}

int host_transport_sync(const char *mode)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "transport_sync %s", mode);
    return host_comm_send_sync(cmd, NULL, 0, 2000);
}

int host_monitor_output(int instance, const char *symbol)
{
    char cmd[HOST_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "monitor_output %d %s", instance, symbol);
    return host_comm_send_sync(cmd, NULL, 0, 2000);
}

int host_cpu_load(float *out)
{
    char val[32];
    int status = host_comm_send_sync("cpu_load", val, sizeof(val), 2000);
    if (status >= 0 && out) *out = (float)atof(val);
    return status;
}

int host_patch_set(int instance, const char *param_uri, const char *path)
{
    char cmd[HOST_CMD_MAX];
    /* mod-host requires the value to be double-quoted */
    snprintf(cmd, sizeof(cmd), "patch_set %d %s \"%s\"", instance, param_uri, path);
    int r = host_comm_send_sync(cmd, NULL, 0, 3000);
    fprintf(stderr, "[host] patch_set %d %s \"%s\" → %d\n", instance, param_uri, path, r);
    return r;
}
