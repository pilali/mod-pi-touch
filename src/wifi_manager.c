/* wifi_manager.c — Thin wrapper around nmcli for the mod-pi-touch WiFi UI.
 *
 * All public functions are synchronous (blocking).  The UI layer is responsible
 * for running them in background threads and marshalling results back to LVGL.
 *
 * Privilege model
 * ───────────────
 * Read-only operations (scan, status, hotspot-is-active) work without elevation.
 * Write operations (connect, hotspot on/off) use "sudo nmcli …".
 * Ensure /etc/sudoers.d/mod-pi-touch grants NOPASSWD access for these commands.
 */

#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ─── Internal helpers ────────────────────────────────────────────────────────── */

/* Trim trailing whitespace in-place. */
static void rtrim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t'))
        s[--n] = '\0';
}

/* Remove backslash-escapes that nmcli -t inserts for ':' and '\'.
 * nmcli terse mode escapes ':' as '\:' and '\' as '\\'. */
static void nmcli_unescape(const char *src, char *dst, size_t dstsz)
{
    size_t di = 0;
    for (const char *p = src; *p && di < dstsz - 1; p++) {
        if (*p == '\\' && *(p+1) != '\0') {
            p++;
            dst[di++] = *p;
        } else {
            dst[di++] = *p;
        }
    }
    dst[di] = '\0';
}

/* Build a single-quoted shell argument, escaping embedded single quotes as '\''.
 * dst must be at least strlen(src)*4 + 3 bytes. */
static void shell_quote(const char *src, char *dst, size_t dstsz)
{
    size_t i = 0;
    if (i < dstsz) dst[i++] = '\'';
    for (const char *p = src; *p && i < dstsz - 4; p++) {
        if (*p == '\'') {
            dst[i++] = '\'';
            dst[i++] = '\\';
            dst[i++] = '\'';
            dst[i++] = '\'';
        } else {
            dst[i++] = *p;
        }
    }
    if (i + 1 < dstsz) dst[i++] = '\'';
    dst[i] = '\0';
}

/* ─── Scan ────────────────────────────────────────────────────────────────────── */

int wifi_scan(wifi_network_t *networks, int max)
{
    if (!networks || max <= 0) return -1;

    /* Request a rescan first (best-effort, may need sudo on some systems). */
    system("nmcli dev wifi rescan 2>/dev/null || "
           "sudo nmcli dev wifi rescan 2>/dev/null; true");

    FILE *fp = popen("nmcli -t -f SSID,SIGNAL,SECURITY dev wifi list 2>/dev/null", "r");
    if (!fp) return -1;

    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp) && count < max) {
        rtrim(line);
        if (line[0] == '\0') continue;

        /* Format: SSID:SIGNAL:SECURITY (nmcli -t -f …) */
        /* Split on un-escaped ':' */
        char ssid_raw[256]     = "";
        char signal_str[16]    = "";
        char security_str[64]  = "";

        /* Manual split respecting nmcli escaping */
        char *fields[3];
        char  tmp[256];
        memcpy(tmp, line, sizeof(tmp));

        int   fi    = 0;
        char *start = tmp;
        char *p;
        for (p = tmp; *p && fi < 2; p++) {
            if (*p == '\\' && *(p+1) != '\0') { p++; continue; } /* skip escaped */
            if (*p == ':') { *p = '\0'; fields[fi++] = start; start = p + 1; }
        }
        fields[fi] = start;  /* last field (may be empty) */

        if (fi < 1) continue;  /* malformed */

        nmcli_unescape(fields[0], ssid_raw, sizeof(ssid_raw));
        if (fi >= 1) snprintf(signal_str,   sizeof(signal_str),   "%s", fields[1]);
        if (fi >= 2) snprintf(security_str, sizeof(security_str), "%s", fields[2]);

        /* Skip empty SSIDs (hidden networks) */
        if (ssid_raw[0] == '\0') continue;

        /* Skip duplicate SSIDs */
        bool dup = false;
        for (int i = 0; i < count; i++)
            if (strcmp(networks[i].ssid, ssid_raw) == 0) { dup = true; break; }
        if (dup) continue;

        snprintf(networks[count].ssid, WIFI_MAX_SSID_LEN, "%s", ssid_raw);
        networks[count].signal  = atoi(signal_str);
        networks[count].secured = (security_str[0] != '\0' &&
                                   strcmp(security_str, "--") != 0);
        count++;
    }
    pclose(fp);
    return count;
}

/* ─── Status ──────────────────────────────────────────────────────────────────── */

bool wifi_get_status(char *ssid_out, int ssid_len,
                     char *ip_out,   int ip_len)
{
    if (ssid_out && ssid_len > 0) ssid_out[0] = '\0';
    if (ip_out   && ip_len   > 0) ip_out[0]   = '\0';

    /* Step 1 — find the active WiFi interface name (e.g. "wlan0").
     * nmcli -t -f DEVICE,TYPE,STATE dev  →  "wlan0:wifi:connected" */
    char iface[32] = "";
    {
        FILE *fp = popen("nmcli -t -f DEVICE,TYPE,STATE dev 2>/dev/null", "r");
        if (!fp) return false;
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            rtrim(line);
            /* Split into at most 3 fields on ':' */
            char *f[3] = { line, NULL, NULL };
            int fi = 0;
            for (char *p = line; *p && fi < 2; p++) {
                if (*p == ':') { *p = '\0'; f[++fi] = p + 1; }
            }
            if (f[1] && strcmp(f[1], "wifi") == 0 &&
                f[2] && strcmp(f[2], "connected") == 0) {
                snprintf(iface, sizeof(iface), "%s", f[0]);
                break;
            }
        }
        pclose(fp);
    }

    if (iface[0] == '\0') return false;

    /* Step 2 — SSID: get the active SSID on that interface.
     * nmcli -t -f ACTIVE,SSID dev wifi list ifname <iface>
     * →  "yes:<ssid>" for the connected network */
    if (ssid_out) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "nmcli -t -f ACTIVE,SSID dev wifi list ifname %s 2>/dev/null",
                 iface);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                rtrim(line);
                if (strncmp(line, "yes:", 4) == 0) {
                    char raw[256];
                    nmcli_unescape(line + 4, raw, sizeof(raw));
                    snprintf(ssid_out, (size_t)ssid_len, "%s", raw);
                    break;
                }
            }
            pclose(fp);
        }
    }

    /* Step 3 — IP address on that interface.
     * nmcli -t -f IP4.ADDRESS dev show <iface>
     * →  "IP4.ADDRESS[1]:<ip>/<prefix>" */
    if (ip_out) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "nmcli -t -f IP4.ADDRESS dev show %s 2>/dev/null", iface);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                rtrim(line);
                if (strncmp(line, "IP4.ADDRESS", 11) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        char *slash = strchr(colon + 1, '/');
                        if (slash) *slash = '\0';
                        snprintf(ip_out, (size_t)ip_len, "%s", colon + 1);
                        break;
                    }
                }
            }
            pclose(fp);
        }
    }

    return true;
}

/* ─── Connect ─────────────────────────────────────────────────────────────────── */

int wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') return -1;

    char ssid_q[512];
    shell_quote(ssid, ssid_q, sizeof(ssid_q));

    char cmd[2048];
    if (password && password[0] != '\0') {
        char pass_q[600];
        shell_quote(password, pass_q, sizeof(pass_q));
        snprintf(cmd, sizeof(cmd),
                 "sudo nmcli dev wifi connect %s password %s 2>&1",
                 ssid_q, pass_q);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "sudo nmcli dev wifi connect %s 2>&1",
                 ssid_q);
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    /* Consume output to avoid broken pipe; check for "successfully" */
    char line[256];
    bool ok = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "successfully") || strstr(line, "activated"))
            ok = true;
    }
    int rc = pclose(fp);
    return (rc == 0 || ok) ? 0 : -1;
}

/* ─── Hotspot ─────────────────────────────────────────────────────────────────── */

bool wifi_hotspot_is_active(void)
{
    FILE *fp = popen("nmcli -t -f NAME,STATE con show --active 2>/dev/null", "r");
    if (!fp) return false;

    bool active = false;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, WIFI_HOTSPOT_CON_NAME ":", strlen(WIFI_HOTSPOT_CON_NAME) + 1) == 0) {
            active = true;
            break;
        }
    }
    pclose(fp);
    return active;
}

int wifi_hotspot_set(bool enabled, const char *password)
{
    char cmd[512];
    if (enabled) {
        const char *pw = (password && password[0]) ? password : WIFI_HOTSPOT_PASSWORD_DEF;
        char pw_q[280];
        shell_quote(pw, pw_q, sizeof(pw_q));
        /* Create (or re-activate) the hotspot profile */
        snprintf(cmd, sizeof(cmd),
                 "sudo nmcli con delete %s 2>/dev/null; "
                 "sudo nmcli dev wifi hotspot "
                 "con-name %s ssid '%s' password %s 2>&1",
                 WIFI_HOTSPOT_CON_NAME,
                 WIFI_HOTSPOT_CON_NAME,
                 WIFI_HOTSPOT_SSID,
                 pw_q);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "sudo nmcli con down %s 2>&1",
                 WIFI_HOTSPOT_CON_NAME);
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    /* Drain output */
    char line[256];
    bool ok = !enabled; /* for disable, assume ok unless error */
    while (fgets(line, sizeof(line), fp)) {
        if (enabled && (strstr(line, "successfully") || strstr(line, "activated")))
            ok = true;
        if (strstr(line, "Error") || strstr(line, "error"))
            ok = false;
    }
    int rc = pclose(fp);
    return (rc == 0 || ok) ? 0 : -1;
}
