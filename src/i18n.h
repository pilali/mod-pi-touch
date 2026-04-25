#pragma once

/* ─── Language identifiers ───────────────────────────────────────────────────── */
typedef enum {
    LANG_EN = 0,
    LANG_FR,
    LANG_COUNT
} mpt_lang_t;

/* ─── Translation key enum ───────────────────────────────────────────────────── */
typedef enum {

    /* Common */
    TR_OK = 0,
    TR_CANCEL,
    TR_ERROR,
    TR_BACK,
    TR_SAVED,
    TR_DELETED,
    TR_ENABLED,
    TR_DISABLED,

    /* Top bar */
    TR_BANKS,
    TR_NO_PEDALBOARD,

    /* Files menu — top bar button label */
    TR_MENU_FILES,

    /* Files menu — action labels */
    TR_MENU_NEW_PB,
    TR_MENU_SAVE_PB,
    TR_MENU_SAVE_PB_AS,
    TR_MENU_SAVE_SNAP,
    TR_MENU_DELETE_SNAP,
    TR_MENU_DELETE_PB,
    TR_MENU_RENAME_PB,
    TR_MENU_NEW_SNAP,
    TR_MENU_REORDER_SNAPS,
    TR_SNAP_REORDER_TITLE,
    TR_CONFIRM_RENAME_PB,

    /* Confirm / message dialogs */
    TR_CONFIRM_SAVE_PB,           /* no %s */
    TR_CONFIRM_SAVE_SNAP,         /* %s = snapshot name */
    TR_CONFIRM_DELETE_SNAP,       /* %s = snapshot name */
    TR_CONFIRM_DELETE_PB,         /* %s = pedalboard name */
    TR_MSG_NO_SNAP,               /* dialog title */
    TR_MSG_SELECT_SNAP_FIRST,
    TR_MSG_PB_SAVED,
    TR_MSG_PB_SAVE_ERROR,
    TR_MSG_PB_LOAD_ERROR,
    TR_MSG_PB_SAVE_FAIL,
    TR_MSG_SNAP_SAVED,
    TR_MSG_SNAP_SAVE_FAIL,
    TR_MSG_SNAP_DELETED,
    TR_MSG_PB_DELETED,
    TR_MSG_NO_PB_LOADED_TITLE,
    TR_MSG_NO_PB_LOADED,

    /* Bank browser */
    TR_BANK_TITLE,
    TR_BANK_ALL,
    TR_BANK_NO_PB_FOUND,
    TR_BANK_NO_PB_IN_BANK,
    TR_BANK_NEW_BANK,
    TR_BANK_NEW_BANK_HINT,
    TR_BANK_CONFIRM_DELETE_TITLE,
    TR_BANK_CONFIRM_DELETE_MSG,
    TR_BANK_ADD_PB,
    TR_BANK_REMOVE_PB_TITLE,
    TR_BANK_REMOVE_PB_MSG,
    TR_BANK_VALIDATE,

    /* Plugin browser */
    TR_PLUGIN_BROWSER_TITLE,
    TR_PLUGIN_SEARCH_HINT,
    TR_PLUGIN_NOT_FOUND,
    TR_PLUGIN_ERROR,
    TR_PLUGIN_LOADING,

    /* Snapshot bar */
    TR_SNAP_LABEL,
    TR_SNAP_NEW_TITLE,
    TR_SNAP_NEW_HINT,
    TR_SNAP_RENAME,
    TR_SNAP_DELETE_BTN,
    TR_SNAP_DELETE_TITLE,

    /* Plugin block context menu */
    TR_PLUG_BYPASS,
    TR_PLUG_ENABLE,
    TR_PLUG_REMOVE,

    /* Param editor */
    TR_PARAM_BYPASS_LABEL,
    TR_PARAM_ENABLED,
    TR_PARAM_DISABLED,
    TR_PARAM_MODEL_FILE,
    TR_PARAM_NONE,
    TR_PARAM_BROWSE,
    TR_PARAM_SELECT_MODEL,
    TR_PARAM_OUTPUT_PORTS,
    TR_PARAM_CV_OUTPUTS,

    /* File browser */
    TR_FILE_SELECT_TITLE,
    TR_FILE_EMPTY,

    /* Pedalboard canvas */
    TR_PB_EMPTY_MSG,
    TR_PB_CHOOSE_INPUT,
    TR_PB_SELECT_OUTPUT,
    TR_PB_TAP_SOURCE,
    TR_PB_SOURCE_OUTPUT,
    TR_PB_CONNECT,
    TR_PB_DISCONNECT,
    TR_PB_DISC_TITLE,          /* "Disconnect from:" */
    TR_PB_DISC_ALL,            /* "All" / "Tout" */

    /* Conductor */
    TR_CONDUCTOR_TITLE,
    TR_CONDUCTOR_TEMPO,
    TR_CONDUCTOR_TIME_SIG,
    TR_CONDUCTOR_CLOCK,
    TR_CONDUCTOR_INTERNAL,
    TR_CONDUCTOR_MIDI_SLAVE,
    TR_CONDUCTOR_PLAY,
    TR_CONDUCTOR_STOP,
    TR_CONDUCTOR_TAP,
    TR_CONDUCTOR_DENOM,

    /* Scene view */
    TR_SCENE_TITLE,
    TR_SCENE_PEDALS,
    TR_SCENE_SETLIST,
    TR_SCENE_UNASSIGNED,
    TR_SCENE_MIDI_LEARNING,
    TR_SCENE_CANCEL_LEARN,
    TR_SCENE_PICK_PLUGIN,
    TR_SCENE_CONFIRM_PB,
    TR_SCENE_BYPASSED,
    TR_SCENE_ACTIVE,
    TR_SCENE_NO_PB,
    TR_SCENE_NO_SNAPS,
    TR_SCENE_UNASSIGN,
    TR_SCENE_LEARN_MIDI,
    TR_SCENE_ALL_BANKS,

    /* Pre-FX (Tuner + Noise Gate) */
    TR_PREFX_TITLE,
    TR_PREFX_TUNER,
    TR_PREFX_NOISEGATE,
    TR_PREFX_REF_FREQ,
    TR_PREFX_INPUT,
    TR_PREFX_MUTE_PB,
    TR_PREFX_GATE_ENABLED,
    TR_PREFX_THRESHOLD,
    TR_PREFX_DECAY,
    TR_PREFX_MODE,
    TR_PREFX_MODE_OFF,
    TR_PREFX_MODE_IN1,
    TR_PREFX_MODE_IN2,
    TR_PREFX_MODE_STEREO,

    /* Settings */
    TR_SETTINGS_TITLE,
    TR_SETTINGS_SYSTEM,
    TR_SETTINGS_AUDIO,
    TR_SETTINGS_MIDI,
    TR_SETTINGS_JACK_RESTARTING,
    TR_SETTINGS_DETECTED_IN,
    TR_SETTINGS_DETECTED_OUT,
    TR_SETTINGS_SAMPLE_RATE,
    TR_SETTINGS_SAMPLE_RATE_VAL,
    TR_SETTINGS_INTERFACE,
    TR_SETTINGS_BUFFER,
    TR_SETTINGS_BUFFER_SIZES,
    TR_SETTINGS_BIT_DEPTH,
    TR_SETTINGS_BIT_DEPTH_OPTS,
    TR_SETTINGS_APPLY_JACK,
    TR_SETTINGS_NO_MIDI,
    TR_SETTINGS_MIDI_LOOPBACK,
    TR_SETTINGS_HOST_ADDR,
    TR_SETTINGS_HOST_PORT,
    TR_SETTINGS_PB_DIR,
    TR_SETTINGS_FB,
    TR_SETTINGS_TOUCH,
    TR_SETTINGS_MOD_HOST,
    TR_SETTINGS_CONNECTED,
    TR_SETTINGS_DISCONNECTED,
    TR_SETTINGS_CPU,
    TR_SETTINGS_CPU_DEFAULT,
    TR_SETTINGS_LANGUAGE,
    TR_SETTINGS_LANGUAGE_OPTS,

    /* WiFi */
    TR_SETTINGS_WIFI,
    TR_SETTINGS_WIFI_CURRENT,
    TR_SETTINGS_WIFI_IP,
    TR_SETTINGS_WIFI_NO_CONNECTION,
    TR_SETTINGS_WIFI_SCAN,
    TR_SETTINGS_WIFI_SCANNING,
    TR_SETTINGS_WIFI_NETWORK,
    TR_SETTINGS_WIFI_PASSWORD,
    TR_SETTINGS_WIFI_CONNECT,
    TR_SETTINGS_WIFI_CONNECTING,
    TR_SETTINGS_WIFI_CONNECTED_OK,
    TR_SETTINGS_WIFI_CONNECT_FAIL,
    TR_SETTINGS_WIFI_HOTSPOT,
    TR_SETTINGS_WIFI_HOTSPOT_ON,
    TR_SETTINGS_WIFI_HOTSPOT_OFF,
    TR_SETTINGS_WIFI_HOTSPOT_SSID,
    TR_SETTINGS_WIFI_HOTSPOT_PASSWORD,
    TR_SETTINGS_WIFI_HOTSPOT_PW_SAVED,

    /* MOD-UI co-existence */
    TR_SETTINGS_MODUI,
    TR_SETTINGS_MODUI_ACTIVATE,
    TR_SETTINGS_MODUI_DEACTIVATE,
    TR_MODUI_ACTIVE_TITLE,
    TR_MODUI_ACTIVE_BODY,
    TR_MODUI_BTN_DISABLE,
    TR_MODUI_SAVE_CONFIRM,  /* "Save pedalboard before switching to MOD-UI?" */

    /* Power */
    TR_SETTINGS_POWER,
    TR_SETTINGS_SHUTDOWN,
    TR_SETTINGS_REBOOT,
    TR_SETTINGS_CONFIRM_SHUTDOWN,
    TR_SETTINGS_CONFIRM_REBOOT,

    /* Splash screen */
    TR_SPLASH_SCANNING,
    TR_SPLASH_BUILDING_UI,
    TR_SPLASH_CONNECTING,
    TR_SPLASH_INIT_FX,
    TR_SPLASH_LOADING_PB,
    TR_SPLASH_READY,
    TR_SPLASH_SHUTTING_DOWN,
    TR_SPLASH_REBOOTING,

    TR_COUNT
} tr_key_t;

/* ─── API ────────────────────────────────────────────────────────────────────── */

/* Set the active language. Call at startup and when the user changes it. */
void        i18n_set_lang(mpt_lang_t lang);
mpt_lang_t  i18n_get_lang(void);

/* Map a 2-letter code ("en", "fr") to a mpt_lang_t; defaults to LANG_EN. */
mpt_lang_t  i18n_lang_from_code(const char *code);

/* 2-letter code for a language ("en", "fr"). */
const char *i18n_lang_code(mpt_lang_t lang);

/* Return translated string for key in the current language. Never NULL. */
const char *i18n_str(tr_key_t key);

/* Convenience macro — the only thing the rest of the code should use. */
#define TR(key) i18n_str(key)
