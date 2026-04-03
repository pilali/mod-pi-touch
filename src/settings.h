#pragma once

#include <stdbool.h>
#include <stddef.h>

/* ─── Default paths (override with env vars) ─────────────────────────────── */
#define MPT_DEFAULT_DATA_DIR          "/home/pi/.mod-pi-touch"
#define MPT_DEFAULT_PEDALBOARDS_DIR   "/home/pi/.pedalboards"
#define MPT_DEFAULT_FACTORY_DIR       "/usr/share/mod/pedalboards"
#define MPT_DEFAULT_LV2_USER_DIR      "/home/pi/.lv2"
#define MPT_DEFAULT_LV2_SYSTEM_DIR    "/usr/lib/lv2"
#define MPT_DEFAULT_BANKS_FILE        "banks.json"
#define MPT_DEFAULT_LAST_STATE_FILE   "last.json"
#define MPT_DEFAULT_PREFS_FILE        "prefs.json"
#define MPT_DEFAULT_PLUGIN_CACHE      "plugin_cache.json"

/* ─── mod-host connection ────────────────────────────────────────────────── */
#define MPT_DEFAULT_HOST_ADDR         "127.0.0.1"
#define MPT_DEFAULT_HOST_CMD_PORT     5555
#define MPT_DEFAULT_HOST_FB_PORT      5556

/* ─── Display ─────────────────────────────────────────────────────────────── */
#define MPT_DEFAULT_FB_DEVICE         "/dev/fb0"
#define MPT_DEFAULT_TOUCH_DEVICE      "/dev/input/event1"  /* Goodix TouchScreen */
#define MPT_DISPLAY_WIDTH             1280   /* logical landscape after 270° rotation */
#define MPT_DISPLAY_HEIGHT            720

typedef struct {
    /* Paths */
    char data_dir[512];
    char pedalboards_dir[512];
    char factory_pedalboards_dir[512];
    char lv2_user_dir[512];
    char lv2_system_dir[512];
    char banks_file[512];         /* full path */
    char last_state_file[512];    /* full path */
    char prefs_file[512];         /* full path */
    char plugin_cache_file[512];  /* full path */

    /* mod-host */
    char host_addr[128];
    int  host_cmd_port;
    int  host_fb_port;

    /* Display/input */
    char fb_device[128];
    char touch_device[128];

    /* Runtime state */
    bool dark_mode;
    int  ui_scale_percent;  /* 100 = normal */
} mpt_settings_t;

/* Load settings from env vars + config file. Safe to call multiple times. */
void settings_init(mpt_settings_t *s);

/* Return global singleton (initialized at startup) */
mpt_settings_t *settings_get(void);

/* Persist user prefs (dark_mode, ui_scale) to prefs.json */
int settings_save_prefs(const mpt_settings_t *s);
