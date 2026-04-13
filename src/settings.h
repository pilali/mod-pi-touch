#pragma once

#include <stdbool.h>
#include <stddef.h>

/* ─── Audio / MIDI settings ──────────────────────────────────────────────── */
#define MPT_MAX_MIDI_PORTS 16

typedef struct {
    char  dev[64];      /* ALSA raw-MIDI device, e.g. "hw:1,0,0" */
    char  label[64];    /* human-readable name */
    bool  is_input;
    bool  is_output;
    bool  enabled;      /* user-selected */
} mpt_midi_port_t;

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
#define MPT_DEFAULT_USER_FILES_DIR    "/home/pistomp/data/user-files"

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
    char user_files_dir[512];     /* MOD user-files root (Aida DSP Models, NAM Models, ...) */

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
    char language[8];       /* "en" or "fr" */

    /* Audio / JACK */
    char jack_audio_device[32];  /* ALSA device, e.g. "hw:1"  (empty = hw:0) */
    int  jack_buffer_size;       /* 32 / 64 / 128 / 256       (0 = default 128) */
    int  jack_bit_depth;         /* 16 or 24                  (0 = default 24) */
    int  audio_capture_ch;       /* capture channels shown on I/O display (0 = auto) */
    int  audio_playback_ch;      /* playback channels shown on I/O display (0 = auto) */

    /* MIDI */
    mpt_midi_port_t midi_ports[MPT_MAX_MIDI_PORTS];
    int             midi_port_count;

    /* pre-fx (tuner + noise gate) */
    bool  gate_enabled;      /* false = bypass */
    float gate_threshold;    /* -70..-10 dB, default -60 */
    float gate_decay;        /* 1-500 ms, default 10 */
    int   gate_mode;         /* 0=Off 1=In1 2=In2 3=Stereo, default 3 */
    float tuner_ref_freq;    /* 220-880 Hz, default 440 */
} mpt_settings_t;

/* Load settings from env vars + config file. Safe to call multiple times. */
void settings_init(mpt_settings_t *s);

/* Return global singleton (initialized at startup) */
mpt_settings_t *settings_get(void);

/* Persist user prefs (dark_mode, ui_scale, audio, MIDI) to prefs.json */
int settings_save_prefs(const mpt_settings_t *s);

/* Restart jackd with the current audio settings.
 * Returns 0 if the command was dispatched (does not guarantee JACK is up). */
int settings_apply_jack(const mpt_settings_t *s);
