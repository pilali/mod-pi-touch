#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Maximum length of a single mod-host command string */
#define HOST_CMD_MAX 4096
/* Maximum response length */
#define HOST_RESP_MAX 4096

/* Callback invoked on the feedback thread when mod-host sends an unsolicited
 * message (parameter monitoring, audio levels, MIDI events). */
typedef void (*host_feedback_cb_t)(const char *msg, void *userdata);

/* Response callback — called from sender thread with the response line.
 * status ≥ 0 → success, < 0 → error code. value may be NULL. */
typedef void (*host_resp_cb_t)(int status, const char *value, void *userdata);

/* ─── Lifecycle ─────────────────────────────────────────────────────────────── */

/* Single-attempt connect — returns 0 on success, -1 if mod-host is not
 * listening yet.  Does not retry; the caller is responsible for the retry
 * loop so it can update UI progress between attempts. */
int  host_comm_try_connect(const char *addr, int cmd_port, int fb_port,
                           host_feedback_cb_t feedback_cb, void *feedback_ud);

/* Connect to mod-host on cmd_port and fb_port (retries up to 20 × 500 ms).
 * feedback_cb is called on a background thread for monitoring messages.
 * Returns 0 on success, -1 on error. */
int  host_comm_connect(const char *addr, int cmd_port, int fb_port,
                       host_feedback_cb_t feedback_cb, void *feedback_ud);

/* Disconnect and free all resources. Blocks until the feedback thread exits. */
void host_comm_disconnect(void);

/* True if the command socket is connected. */
bool host_comm_is_connected(void);

/* ─── Command sending ────────────────────────────────────────────────────────── */

/* Send a raw command to mod-host and invoke cb with the parsed response.
 * cmd must be a null-terminated string; the trailing null byte is appended
 * automatically. Thread-safe: may be called from any thread. */
int host_comm_send(const char *cmd, host_resp_cb_t cb, void *userdata);

/* Convenience: synchronous send, blocks until response.
 * Returns status code; copies value into val_buf (may be NULL).
 * Timeout in ms (0 = no timeout). */
int host_comm_send_sync(const char *cmd, char *val_buf, size_t val_bufsz, int timeout_ms);

/* ─── High-level helpers ──────────────────────────────────────────────────────── */

int host_remove_all(void);
int host_add_plugin(int instance, const char *uri);
int host_remove_plugin(int instance);
int host_bypass(int instance, bool bypass);
int host_param_set(int instance, const char *symbol, float value);
int host_param_get(int instance, const char *symbol, float *out);
int host_connect(const char *port_a, const char *port_b);
int host_disconnect(const char *port_a, const char *port_b);
int host_disconnect_all(const char *port);
int host_state_load(const char *dir);
int host_state_save(const char *dir);
int host_preset_load(int instance, const char *preset_uri);
int host_preset_save(int instance, const char *name, const char *dir, const char *filename);
int host_midi_map(int instance, const char *symbol, int channel, int cc,
                  float min, float max);
int host_midi_unmap(int instance, const char *symbol);
/* Enter MIDI learn mode: mod-host will send "midi_mapped" feedback on the
 * next received MIDI CC and map it to the parameter. */
int host_midi_learn(int instance, const char *symbol, float min, float max);
int host_transport(bool rolling, float bpb, float bpm);
/* Set transport sync source: "none" (internal) or "midi" (MIDI slave). */
int host_transport_sync(const char *mode);
int host_cpu_load(float *out);

/* Subscribe to real-time monitoring of a control output port.
 * mod-host sends "output_set <instance> <symbol> <value>" on port 5556 whenever
 * the port value changes. */
int host_monitor_output(int instance, const char *symbol);
/* Kick-start or continue the feedback loop after all monitor_output calls. */
int host_output_data_ready(void);

/* Send a patch:writable parameter (file path) to a plugin instance.
 * Sends: patch_set <instance> <param_uri> <path> */
int host_patch_set(int instance, const char *param_uri, const char *path);
