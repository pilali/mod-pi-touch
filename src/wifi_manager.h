#pragma once

#include <stdbool.h>

/* ─── Network entry ───────────────────────────────────────────────────────────── */
#define WIFI_MAX_NETWORKS  32
#define WIFI_MAX_SSID_LEN  64

typedef struct {
    char ssid[WIFI_MAX_SSID_LEN];
    int  signal;    /* 0–100 */
    bool secured;
} wifi_network_t;

/* ─── Scan ────────────────────────────────────────────────────────────────────── */

/* Scan available networks — synchronous, may take a few seconds.
 * No elevated privileges required.
 * Returns number of networks found (≥0), or -1 on error. */
int  wifi_scan(wifi_network_t *networks, int max);

/* ─── Status ──────────────────────────────────────────────────────────────────── */

/* Get current connection status — synchronous.
 * ssid_out / ip_out may be NULL.
 * Returns true if connected to a network (excluding hotspot AP). */
bool wifi_get_status(char *ssid_out, int ssid_len,
                     char *ip_out,   int ip_len);

/* ─── Connect ─────────────────────────────────────────────────────────────────── */

/* Connect to a WiFi network — synchronous, blocks until done (up to ~15 s).
 * password may be NULL for open networks.
 * Requires: sudo nmcli privilege (see /etc/sudoers.d/mod-pi-touch).
 * Returns 0 on success, -1 on failure. */
int  wifi_connect(const char *ssid, const char *password);

/* ─── Hotspot ─────────────────────────────────────────────────────────────────── */

#define WIFI_HOTSPOT_CON_NAME      "MPT-Hotspot"
#define WIFI_HOTSPOT_SSID          "ModPiTouch"
#define WIFI_HOTSPOT_PASSWORD_DEF  "modpitouch"
#define WIFI_HOTSPOT_PASSWORD_LEN  64

/* Enable or disable the access-point hotspot — synchronous.
 * password: the WPA2 passphrase to use (NULL → default "modpitouch").
 *           Must be 8–63 printable ASCII characters.
 * Requires: sudo nmcli privilege.
 * Returns 0 on success, -1 on failure. */
int  wifi_hotspot_set(bool enabled, const char *password);

/* Returns true if the hotspot connection is currently active. */
bool wifi_hotspot_is_active(void);
