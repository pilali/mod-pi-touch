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

    /* Save menu — button labels */
    TR_MENU_SAVE_PB,
    TR_MENU_SAVE_PB_AS,
    TR_MENU_SAVE_SNAP,
    TR_MENU_DELETE_SNAP,
    TR_MENU_DELETE_PB,

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
