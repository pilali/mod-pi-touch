#include "ui_param_editor.h"
#include "ui_pedalboard.h"
#include "ui_file_browser.h"
#include "ui_app.h"
#include "../host_comm.h"
#include "../plugin_manager.h"
#include "../settings.h"
#include "../i18n.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

/* ─── File type → (user-files subdir, extensions[]) mapping ─────────────────── */
typedef struct {
    const char *type;
    const char *subdir;
    const char *exts[8]; /* NULL-terminated */
} filetype_info_t;

static const filetype_info_t FILETYPE_MAP[] = {
    { "aidadspmodel", "Aida DSP Models",       { ".aidax", ".json", NULL } },
    { "nammodel",     "NAM Models",             { ".nam",   NULL } },
    { "ir",           "Reverb IRs",             { ".wav", ".flac", ".aiff", ".ogg", NULL } },
    { "cabsim",       "Speaker Cabinets IRs",   { ".wav", ".flac", ".aiff", ".ogg", NULL } },
    { "sf2",          "SF2 Instruments",        { ".sf2", ".sf3", NULL } },
    { "sfz",          "SFZ Instruments",        { ".sfz", NULL } },
    { "audioloop",    "Audio Loops",            { ".wav", ".flac", ".aiff", ".ogg", ".mp3", NULL } },
    { "audiorecording","Audio Recordings",      { ".wav", ".flac", ".aiff", ".ogg", ".mp3", NULL } },
    { "audiotrack",   "Audio Tracks",           { ".wav", ".flac", ".aiff", ".ogg", ".mp3", NULL } },
    { "midiclip",     "MIDI Clips",             { ".mid", ".midi", NULL } },
    { "midisong",     "MIDI Songs",             { ".mid", ".midi", NULL } },
    { NULL, NULL, { NULL } }
};

static const filetype_info_t *find_filetype(const char *type)
{
    for (int i = 0; FILETYPE_MAP[i].type; i++)
        if (strcmp(FILETYPE_MAP[i].type, type) == 0)
            return &FILETYPE_MAP[i];
    return NULL;
}

/* Build start_dir and extensions from a comma-separated file_types string.
 * exts_out must hold at least 32 pointers. Returns number of extensions. */
static int resolve_file_types(const char *file_types,
                               const char *user_files_root,
                               char *start_dir_out, size_t start_dir_sz,
                               const char **exts_out, int exts_max)
{
    /* Parse comma-separated types */
    char types_buf[256];
    snprintf(types_buf, sizeof(types_buf), "%s", file_types);

    const char *subdirs[16];
    int n_subdirs = 0;
    int n_exts = 0;

    char *tok = strtok(types_buf, ",");
    while (tok) {
        /* Trim spaces */
        while (*tok == ' ') tok++;
        const filetype_info_t *fi = find_filetype(tok);
        if (fi) {
            /* Add subdir if not already in list */
            bool dup_dir = false;
            for (int i = 0; i < n_subdirs; i++)
                if (strcmp(subdirs[i], fi->subdir) == 0) { dup_dir = true; break; }
            if (!dup_dir && n_subdirs < 16)
                subdirs[n_subdirs++] = fi->subdir;

            /* Add extensions if not already in list */
            for (int j = 0; fi->exts[j] && n_exts < exts_max - 1; j++) {
                bool dup = false;
                for (int k = 0; k < n_exts; k++)
                    if (strcmp(exts_out[k], fi->exts[j]) == 0) { dup = true; break; }
                if (!dup) exts_out[n_exts++] = fi->exts[j];
            }
        }
        tok = strtok(NULL, ",");
    }
    exts_out[n_exts] = NULL;

    /* Start dir: if single subdir, go directly there; otherwise user_files_root */
    if (n_subdirs == 1)
        snprintf(start_dir_out, start_dir_sz, "%s/%s", user_files_root, subdirs[0]);
    else
        snprintf(start_dir_out, start_dir_sz, "%s", user_files_root);

    return n_exts;
}

/* ─── Widget type tags ──────────────────────────────────────────────────────── */
#define CTRL_ARC    0
#define CTRL_TOGGLE 1
#define CTRL_ENUM   2
#define CTRL_METER  3  /* read-only output port bar */

/* ─── Control registry — static pool, no malloc ─────────────────────────────── */
#define MAX_CONTROLS 64
typedef struct {
    char      symbol[PB_SYMBOL_MAX];
    int       type;           /* CTRL_ARC / CTRL_TOGGLE / CTRL_ENUM / CTRL_METER */
    lv_obj_t *widget;         /* arc / switch / dropdown (NULL for CTRL_METER) */
    lv_obj_t *val_lbl;        /* numeric label */
    float     min, max;       /* physical range */
    bool      is_integer;     /* arc: round before sending */
    float     enum_values[16];
    int       enum_count;
    /* MIDI CC mapping (unused for CTRL_METER) */
    lv_obj_t *midi_lbl;
    int       midi_channel;
    int       midi_cc;
    /* CTRL_METER only */
    lv_obj_t *meter_bar;      /* lv_bar widget */
} ctrl_reg_t;

static ctrl_reg_t g_controls[MAX_CONTROLS];
static int        g_ctrl_count = 0;

/* ─── Patch param state ──────────────────────────────────────────────────────── */
#define MAX_PATCH_PARAMS 8
typedef struct {
    char      param_uri[512];        /* patch:writable parameter URI */
    char      current_path[1024];    /* currently loaded file path */
    char      default_dir[1024];     /* default browse root directory */
    char      file_types[256];       /* comma-separated mod:fileTypes */
    lv_obj_t *path_lbl;             /* label showing current filename */
} patch_reg_t;
static patch_reg_t g_patch_params[MAX_PATCH_PARAMS];
static int         g_patch_param_count = 0;

/* ─── Editor state ───────────────────────────────────────────────────────────── */
static char                g_user_files_dir[512] = {0};

static lv_obj_t           *g_modal      = NULL;
static int                 g_instance   = -1;
static bool                g_enabled    = true;
static bypass_toggle_cb_t  g_bypass_cb  = NULL;
static void               *g_bypass_ud  = NULL;
static lv_obj_t           *g_bypass_lbl = NULL;
static param_change_cb_t   g_value_cb   = NULL;
static void               *g_value_ud   = NULL;
static patch_change_cb_t   g_patch_cb   = NULL;
static void               *g_patch_ud   = NULL;
static midi_map_cb_t       g_midi_cb    = NULL;
static void               *g_midi_ud    = NULL;

/* Bypass MIDI state */
static lv_obj_t           *g_bypass_midi_lbl = NULL;
static int                 g_bypass_midi_ch  = -1;
static int                 g_bypass_midi_cc  = -1;

/* Symbol currently in MIDI learn mode ("" = none) */
static char g_learning_symbol[PB_SYMBOL_MAX] = {0};

/* ─── Helpers ─────────────────────────────────────────────────────────────────── */

/* Find pm port info by symbol; returns NULL if not found */
static const pm_port_info_t *find_pm_port(const pm_plugin_info_t *info,
                                          const char *sym)
{
    if (!info) return NULL;
    for (int i = 0; i < info->port_count; i++)
        if (strcmp(info->ports[i].symbol, sym) == 0)
            return &info->ports[i];
    return NULL;
}

/* Find ctrl_reg_t by symbol; returns NULL if not found */
static ctrl_reg_t *find_ctrl(const char *sym)
{
    for (int i = 0; i < g_ctrl_count; i++)
        if (strcmp(g_controls[i].symbol, sym) == 0)
            return &g_controls[i];
    return NULL;
}

/* ─── MIDI chip helpers ───────────────────────────────────────────────────────── */

/* Format MIDI assignment into buf. */
static void fmt_midi(char *buf, size_t bufsz, int ch, int cc)
{
    if (ch < 0 || cc < 0)
        snprintf(buf, bufsz, " — ");
    else
        snprintf(buf, bufsz, "CC%d ch%d", cc, ch + 1);
}

/* Create a small MIDI chip button at the bottom of a card.
 * reg is the ctrl_reg_t* or NULL (bypass case, uses separate globals). */
typedef struct { ctrl_reg_t *reg; bool is_bypass; } midi_chip_ud_t;
static midi_chip_ud_t g_midi_chip_uds[MAX_CONTROLS + 1]; /* +1 for bypass */
static int            g_midi_chip_ud_count = 0;

static void midi_chip_tap_cb(lv_event_t *e)
{
    midi_chip_ud_t *ud = lv_event_get_user_data(e);
    if (!ud) return;

    const char *sym;
    float  min, max;
    int    has_mapping;

    if (ud->is_bypass) {
        sym         = ":bypass";
        min         = 0.0f; max = 1.0f;
        has_mapping = g_bypass_midi_cc >= 0;
    } else {
        ctrl_reg_t *reg = ud->reg;
        sym         = reg->symbol;
        min         = reg->min; max = reg->max;
        has_mapping = reg->midi_cc >= 0;
    }

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_LONG_PRESSED && has_mapping) {
        /* Unmap */
        host_midi_unmap(g_instance, sym);
        if (ud->is_bypass) {
            g_bypass_midi_ch = -1;
            g_bypass_midi_cc = -1;
            if (g_bypass_midi_lbl) lv_label_set_text(g_bypass_midi_lbl, " — ");
        } else {
            ud->reg->midi_channel = -1;
            ud->reg->midi_cc      = -1;
            if (ud->reg->midi_lbl) lv_label_set_text(ud->reg->midi_lbl, " — ");
        }
        g_learning_symbol[0] = '\0';
        if (g_midi_cb)
            g_midi_cb(g_instance, sym, -1, -1, min, max, g_midi_ud);
    } else if (code == LV_EVENT_CLICKED) {
        /* Enter MIDI learn */
        snprintf(g_learning_symbol, sizeof(g_learning_symbol), "%s", sym);
        host_midi_learn(g_instance, sym, min, max);
        /* Show waiting indicator */
        lv_obj_t *lbl = ud->is_bypass ? g_bypass_midi_lbl
                                      : ud->reg->midi_lbl;
        if (lbl) lv_label_set_text(lbl, " … ");
    }
}

/* Build a MIDI chip and return the label inside it.
 * ud_idx selects the pre-allocated midi_chip_ud_t slot. */
static lv_obj_t *make_midi_chip(lv_obj_t *parent, int midi_ch, int midi_cc,
                                int ud_idx)
{
    midi_chip_ud_t *ud = &g_midi_chip_uds[ud_idx];

    lv_obj_t *chip = lv_btn_create(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(chip, 11, 0);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 3, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x2a3040), 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0x404860), 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_add_event_cb(chip, midi_chip_tap_cb, LV_EVENT_CLICKED, ud);
    lv_obj_add_event_cb(chip, midi_chip_tap_cb, LV_EVENT_LONG_PRESSED, ud);

    char buf[24];
    fmt_midi(buf, sizeof(buf), midi_ch, midi_cc);
    lv_obj_t *lbl = lv_label_create(chip);
    lv_label_set_text(lbl, buf);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x8090b0), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    return lbl;
}

/* ─── Bypass toggle ──────────────────────────────────────────────────────────── */

static void bypass_btn_cb(lv_event_t *e)
{
    (void)e;
    g_enabled = !g_enabled;
    lv_label_set_text(g_bypass_lbl, g_enabled ? TR(TR_PARAM_ENABLED) : TR(TR_PARAM_DISABLED));
    lv_obj_t *btn = lv_obj_get_parent(g_bypass_lbl);
    lv_obj_set_style_bg_color(btn, g_enabled ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
    if (g_bypass_cb) g_bypass_cb(g_bypass_ud);
}

/* ─── Arc (knob) callback ─────────────────────────────────────────────────────── */

static void arc_changed_cb(lv_event_t *e)
{
    ctrl_reg_t *reg = lv_event_get_user_data(e);
    lv_obj_t   *arc = lv_event_get_target(e);
    int arc_val = lv_arc_get_value(arc);

    float ratio = arc_val / 100.0f;
    float value = reg->min + ratio * (reg->max - reg->min);
    if (reg->is_integer) value = (float)(int)(value + 0.5f);

    char buf[32];
    if (reg->is_integer)
        snprintf(buf, sizeof(buf), "%d", (int)value);
    else
        snprintf(buf, sizeof(buf), "%.3g", (double)value);

    if (reg->val_lbl) lv_label_set_text(reg->val_lbl, buf);
    if (g_instance >= 0) host_param_set(g_instance, reg->symbol, value);
    if (g_value_cb) g_value_cb(g_instance, reg->symbol, value, g_value_ud);
}

/* ─── Toggle (switch) callback ───────────────────────────────────────────────── */

static void toggle_changed_cb(lv_event_t *e)
{
    ctrl_reg_t *reg = lv_event_get_user_data(e);
    lv_obj_t   *sw  = lv_event_get_target(e);
    float value = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1.0f : 0.0f;
    if (g_instance >= 0) host_param_set(g_instance, reg->symbol, value);
    if (g_value_cb) g_value_cb(g_instance, reg->symbol, value, g_value_ud);
}

/* ─── Enum (dropdown) callback ───────────────────────────────────────────────── */

static void enum_changed_cb(lv_event_t *e)
{
    ctrl_reg_t *reg = lv_event_get_user_data(e);
    lv_obj_t   *dd  = lv_event_get_target(e);
    int idx = (int)lv_dropdown_get_selected(dd);
    if (idx < 0 || idx >= reg->enum_count) return;
    float value = reg->enum_values[idx];
    if (g_instance >= 0) host_param_set(g_instance, reg->symbol, value);
    if (g_value_cb) g_value_cb(g_instance, reg->symbol, value, g_value_ud);
}

/* ─── File browser callback ──────────────────────────────────────────────────── */

static void on_file_selected(const char *full_path, void *userdata)
{
    patch_reg_t *reg = userdata;
    if (!full_path || !reg) return; /* cancelled */

    snprintf(reg->current_path, sizeof(reg->current_path), "%s", full_path);

    /* Show just the filename in the label */
    const char *basename = strrchr(full_path, '/');
    if (reg->path_lbl)
        lv_label_set_text(reg->path_lbl, basename ? basename + 1 : full_path);

    /* Send to mod-host */
    if (g_instance >= 0)
        host_patch_set(g_instance, reg->param_uri, full_path);

    /* Notify caller to persist the path */
    if (g_patch_cb)
        g_patch_cb(g_instance, reg->param_uri, full_path, g_patch_ud);
}

static void on_browse_btn(lv_event_t *e)
{
    patch_reg_t *reg = lv_event_get_user_data(e);
    if (!reg) return;

    /* Build start_dir and extension filter from file_types */
    char start_dir[1024] = {0};
    const char *exts[32] = {NULL};

    if (reg->file_types[0] && g_user_files_dir[0]) {
        resolve_file_types(reg->file_types, g_user_files_dir,
                           start_dir, sizeof(start_dir),
                           exts, 32);
    }

    /* If user already has a file loaded, prefer its directory */
    if (reg->current_path[0]) {
        char cur_dir[1024];
        snprintf(cur_dir, sizeof(cur_dir), "%s", reg->current_path);
        char *sl = strrchr(cur_dir, '/');
        if (sl && sl != cur_dir) {
            *sl = '\0';
            snprintf(start_dir, sizeof(start_dir), "%s", cur_dir);
        }
    }

    if (!start_dir[0]) snprintf(start_dir, sizeof(start_dir), "%s",
                                reg->default_dir[0] ? reg->default_dir : "/");

    ui_file_browser_open(start_dir, TR(TR_PARAM_SELECT_MODEL),
                         exts[0] ? exts : NULL,
                         on_file_selected, reg);
}

/* ─── Close ──────────────────────────────────────────────────────────────────── */

static void close_cb(lv_event_t *e)
{
    (void)e;
    /* Use async: this button is a child of g_modal — deleting the parent
     * synchronously from a child's event callback causes LVGL to access
     * freed memory while still dispatching the event. */
    if (g_modal) {
        lv_obj_delete_async(g_modal);
        g_modal              = NULL;
        g_instance           = -1;
        g_ctrl_count         = 0;
        g_patch_param_count  = 0;
        g_midi_chip_ud_count = 0;
        g_bypass_midi_lbl    = NULL;
        g_learning_symbol[0] = '\0';
    }
}

/* ─── Card builder helpers ───────────────────────────────────────────────────── */

static lv_obj_t *make_card(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 6, 0);
    return card;
}

static lv_obj_t *make_name_lbl(lv_obj_t *card, const char *name)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, name);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    return lbl;
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_param_editor_show(int instance_id,
                          const char *plugin_label,
                          const char *plugin_uri,
                          pb_port_t *ports, int port_count,
                          pb_patch_t *patch_params, int patch_param_count,
                          bool enabled,
                          bypass_toggle_cb_t bypass_cb, void *bypass_ud,
                          param_change_cb_t value_cb, void *value_ud,
                          patch_change_cb_t patch_cb, void *patch_ud,
                          midi_map_cb_t midi_cb, void *midi_ud)
{
    if (g_modal) ui_param_editor_close();

    g_instance          = instance_id;
    g_enabled           = enabled;
    g_bypass_cb         = bypass_cb;
    g_bypass_ud         = bypass_ud;
    g_bypass_lbl        = NULL;
    g_value_cb          = value_cb;
    g_value_ud          = value_ud;
    g_patch_cb          = patch_cb;
    g_patch_ud          = patch_ud;
    g_midi_cb           = midi_cb;
    g_midi_ud           = midi_ud;
    g_ctrl_count        = 0;
    g_patch_param_count = 0;
    g_midi_chip_ud_count = 0;
    g_bypass_midi_lbl   = NULL;
    g_bypass_midi_ch    = -1;
    g_bypass_midi_cc    = -1;
    g_learning_symbol[0] = '\0';

    /* Read bypass MIDI CC from the :bypass port if present */
    for (int i = 0; i < port_count; i++) {
        if (strcmp(ports[i].symbol, ":bypass") == 0) {
            g_bypass_midi_ch = ports[i].midi_channel;
            g_bypass_midi_cc = ports[i].midi_cc;
            break;
        }
    }

    /* Cache user_files_dir from settings (read once) */
    if (!g_user_files_dir[0]) {
        mpt_settings_t *s = settings_get();
        if (s) snprintf(g_user_files_dir, sizeof(g_user_files_dir),
                        "%s", s->user_files_dir);
    }

    const pm_plugin_info_t *pm_info = pm_plugin_by_uri(plugin_uri);

    /* ── Modal overlay ── */
    g_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_70, 0);

    /* ── Panel ── */
    lv_obj_t *panel = lv_obj_create(g_modal);
    lv_obj_set_size(panel, 900, 560);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 8, 0);

    /* ── Title bar ── */
    lv_obj_t *title_row = lv_obj_create(panel);
    lv_obj_set_size(title_row, LV_PCT(100), 44);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);

    lv_obj_t *title_lbl = lv_label_create(title_row);
    lv_label_set_text(title_lbl, plugin_label);
    lv_obj_set_style_text_color(title_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);

    lv_obj_t *btn_close = lv_btn_create(title_row);
    lv_obj_set_size(btn_close, 36, 36);
    lv_obj_set_style_bg_color(btn_close, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_close);
    lv_obj_add_event_cb(btn_close, close_cb, LV_EVENT_CLICKED, NULL);

    /* ── Scrollable params grid ── */
    lv_obj_t *scroll = lv_obj_create(panel);
    lv_obj_set_size(scroll, LV_PCT(100), 0);
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(scroll, 12, 0);
    lv_obj_set_style_pad_column(scroll, 12, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Bypass toggle — always first ── */
    {
        lv_obj_t *card = make_card(scroll, 140, 145);
        make_name_lbl(card, TR(TR_PARAM_BYPASS_LABEL));

        lv_obj_t *btn = lv_btn_create(card);
        lv_obj_set_size(btn, 110, 44);
        lv_obj_set_style_bg_color(btn, g_enabled ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
        g_bypass_lbl = lv_label_create(btn);
        lv_label_set_text(g_bypass_lbl, g_enabled ? TR(TR_PARAM_ENABLED) : TR(TR_PARAM_DISABLED));
        lv_obj_center(g_bypass_lbl);
        lv_obj_set_style_text_font(g_bypass_lbl, &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(btn, bypass_btn_cb, LV_EVENT_CLICKED, NULL);

        /* MIDI chip for bypass */
        int ud_idx = g_midi_chip_ud_count++;
        g_midi_chip_uds[ud_idx].reg       = NULL;
        g_midi_chip_uds[ud_idx].is_bypass = true;
        g_bypass_midi_lbl = make_midi_chip(card, g_bypass_midi_ch,
                                           g_bypass_midi_cc, ud_idx);
    }

    /* ── One widget per control input port ── */
    for (int i = 0; i < port_count && g_ctrl_count < MAX_CONTROLS; i++) {
        pb_port_t *port = &ports[i];

        /* Look up LV2 port metadata */
        const pm_port_info_t *pm_port = find_pm_port(pm_info, port->symbol);

        /* Only show control input ports; skip audio/MIDI/CV ports */
        if (pm_info != NULL) {
            if (!pm_port) continue;
            if (pm_port->type != PM_PORT_CONTROL_IN) continue;
        }

        /* Port display name */
        const char *disp_name = (pm_port && pm_port->name[0])
                                 ? pm_port->name : port->symbol;

        /* Register entry */
        ctrl_reg_t *reg = &g_controls[g_ctrl_count++];
        memset(reg, 0, sizeof(*reg));
        snprintf(reg->symbol, sizeof(reg->symbol), "%s", port->symbol);
        reg->midi_channel = port->midi_channel;
        reg->midi_cc      = port->midi_cc;

        /* Physical min/max from pm (TTL doesn't store them) */
        float p_min = pm_port ? pm_port->min : 0.0f;
        float p_max = pm_port ? pm_port->max : 1.0f;
        if (p_max <= p_min) p_max = p_min + 1.0f; /* safety */

        /* ── Determine widget type ── */
        bool is_toggle = pm_port && pm_port->toggled;
        bool is_enum   = pm_port && pm_port->enumeration && pm_port->enum_count > 0;

        if (is_toggle) {
            /* ── Toggle switch ── */
            reg->type = CTRL_TOGGLE;
            lv_obj_t *card = make_card(scroll, 140, 135);
            make_name_lbl(card, disp_name);

            lv_obj_t *sw = lv_switch_create(card);
            lv_obj_set_size(sw, 80, 40);
            lv_obj_set_style_bg_color(sw, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
            if (port->value >= 0.5f)
                lv_obj_add_state(sw, LV_STATE_CHECKED);
            lv_obj_add_event_cb(sw, toggle_changed_cb, LV_EVENT_VALUE_CHANGED, reg);
            reg->widget = sw;

            /* MIDI chip */
            int ud_idx = g_midi_chip_ud_count++;
            g_midi_chip_uds[ud_idx].reg       = reg;
            g_midi_chip_uds[ud_idx].is_bypass = false;
            reg->min = 0.0f; reg->max = 1.0f; /* toggles are 0/1 */
            reg->midi_lbl = make_midi_chip(card, reg->midi_channel,
                                           reg->midi_cc, ud_idx);

        } else if (is_enum) {
            /* ── Enum dropdown ── */
            reg->type = CTRL_ENUM;
            reg->enum_count = pm_port->enum_count;
            /* Copy enum values; build options string */
            char options[512] = {0};
            int cur_sel = 0;
            for (int k = 0; k < pm_port->enum_count; k++) {
                reg->enum_values[k] = pm_port->enum_values[k];
                if (k > 0) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
                strncat(options, pm_port->enum_labels[k],
                        sizeof(options) - strlen(options) - 1);
                /* Select closest value */
                if (k == 0 ||
                    fabsf(pm_port->enum_values[k] - port->value) <
                    fabsf(pm_port->enum_values[cur_sel] - port->value))
                    cur_sel = k;
            }

            lv_obj_t *card = make_card(scroll, 220, 135);
            make_name_lbl(card, disp_name);

            lv_obj_t *dd = lv_dropdown_create(card);
            lv_dropdown_set_options(dd, options);
            lv_dropdown_set_selected(dd, (uint16_t)cur_sel);
            lv_obj_set_width(dd, 200);
            lv_obj_set_style_text_font(dd, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_font(dd, &lv_font_montserrat_12,
                                       LV_PART_SELECTED);
            lv_obj_add_event_cb(dd, enum_changed_cb, LV_EVENT_VALUE_CHANGED, reg);
            reg->widget = dd;

            /* MIDI chip */
            {
                int ud_idx = g_midi_chip_ud_count++;
                g_midi_chip_uds[ud_idx].reg       = reg;
                g_midi_chip_uds[ud_idx].is_bypass = false;
                reg->midi_lbl = make_midi_chip(card, reg->midi_channel,
                                               reg->midi_cc, ud_idx);
            }

        } else {
            /* ── Arc knob ── */
            reg->type = CTRL_ARC;
            reg->min = p_min;
            reg->max = p_max;
            reg->is_integer = pm_port && pm_port->integer;

            /* Map current value → 0-100 */
            float range = p_max - p_min;
            int arc_val = (int)(((port->value - p_min) / range) * 100.0f + 0.5f);
            if (arc_val < 0)   arc_val = 0;
            if (arc_val > 100) arc_val = 100;

            lv_obj_t *card = make_card(scroll, 140, 195);
            make_name_lbl(card, disp_name);

            lv_obj_t *arc = lv_arc_create(card);
            lv_obj_set_size(arc, 90, 90);
            lv_arc_set_range(arc, 0, 100);
            lv_arc_set_value(arc, arc_val);
            lv_obj_set_style_arc_color(arc, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
            lv_obj_add_event_cb(arc, arc_changed_cb, LV_EVENT_VALUE_CHANGED, reg);
            reg->widget = arc;

            /* Value label */
            char val_str[32];
            if (reg->is_integer)
                snprintf(val_str, sizeof(val_str), "%d", (int)port->value);
            else
                snprintf(val_str, sizeof(val_str), "%.3g", (double)port->value);
            lv_obj_t *val_lbl = lv_label_create(card);
            lv_label_set_text(val_lbl, val_str);
            lv_obj_set_style_text_color(val_lbl, UI_COLOR_TEXT, 0);
            lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, 0);
            reg->val_lbl = val_lbl;

            /* MIDI chip */
            {
                int ud_idx = g_midi_chip_ud_count++;
                g_midi_chip_uds[ud_idx].reg       = reg;
                g_midi_chip_uds[ud_idx].is_bypass = false;
                reg->midi_lbl = make_midi_chip(card, reg->midi_channel,
                                               reg->midi_cc, ud_idx);
            }
        }
    }

    /* ── Patch parameter cards (file path selectors) ── */
    if (pm_info) {
        for (int i = 0; i < pm_info->patch_param_count
                        && g_patch_param_count < MAX_PATCH_PARAMS; i++) {
            const pm_patch_param_t *pp = &pm_info->patch_params[i];

            patch_reg_t *preg = &g_patch_params[g_patch_param_count++];
            memset(preg, 0, sizeof(*preg));
            snprintf(preg->param_uri,   sizeof(preg->param_uri),   "%s", pp->uri);
            snprintf(preg->default_dir, sizeof(preg->default_dir), "%s", pp->default_dir);
            snprintf(preg->file_types,  sizeof(preg->file_types),  "%s", pp->file_types);

            /* Find current path from pb_patch_params if available */
            for (int j = 0; j < patch_param_count; j++) {
                if (strcmp(patch_params[j].uri, pp->uri) == 0) {
                    snprintf(preg->current_path, sizeof(preg->current_path),
                             "%s", patch_params[j].path);
                    break;
                }
            }

            /* Wide card for file selector */
            lv_obj_t *card = make_card(scroll, 400, 110);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(card, 10, 0);

            /* Label column */
            lv_obj_t *label_col = lv_obj_create(card);
            lv_obj_set_size(label_col, 200, LV_PCT(100));
            lv_obj_set_style_bg_opa(label_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(label_col, 0, 0);
            lv_obj_set_style_pad_all(label_col, 0, 0);
            lv_obj_set_flex_flow(label_col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(label_col, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

            lv_obj_t *name_lbl = lv_label_create(label_col);
            lv_label_set_text(name_lbl, pp->label[0] ? pp->label : TR(TR_PARAM_MODEL_FILE));
            lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT_DIM, 0);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);

            /* Current filename */
            const char *cur_name = TR(TR_PARAM_NONE);
            if (preg->current_path[0]) {
                const char *sl = strrchr(preg->current_path, '/');
                cur_name = sl ? sl + 1 : preg->current_path;
            }
            preg->path_lbl = lv_label_create(label_col);
            lv_label_set_text(preg->path_lbl, cur_name);
            lv_label_set_long_mode(preg->path_lbl, LV_LABEL_LONG_DOT);
            lv_obj_set_width(preg->path_lbl, 190);
            lv_obj_set_style_text_color(preg->path_lbl, UI_COLOR_TEXT, 0);
            lv_obj_set_style_text_font(preg->path_lbl, &lv_font_montserrat_12, 0);

            /* Browse button */
            lv_obj_t *browse_btn = lv_btn_create(card);
            lv_obj_set_size(browse_btn, 120, 50);
            lv_obj_set_style_bg_color(browse_btn, UI_COLOR_PRIMARY, 0);
            lv_obj_t *browse_lbl = lv_label_create(browse_btn);
            lv_label_set_text_fmt(browse_lbl, LV_SYMBOL_DIRECTORY "%s", TR(TR_PARAM_BROWSE));
            lv_obj_center(browse_lbl);
            lv_obj_set_style_text_font(browse_lbl, &lv_font_montserrat_14, 0);
            lv_obj_add_event_cb(browse_btn, on_browse_btn, LV_EVENT_CLICKED, preg);
        }
    }

    /* ── Output port meters (read-only, real-time feedback) ── */
    if (pm_info) {
        bool has_out = false;
        for (int i = 0; i < pm_info->port_count; i++)
            if (pm_info->ports[i].type == PM_PORT_CONTROL_OUT) { has_out = true; break; }

        if (has_out) {
            /* Separator line */
            lv_obj_t *sep = lv_obj_create(scroll);
            lv_obj_set_size(sep, LV_PCT(95), 1);
            lv_obj_set_style_bg_color(sep, UI_COLOR_TEXT_DIM, 0);
            lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(sep, 0, 0);
            lv_obj_set_style_pad_all(sep, 0, 0);

            /* Section title */
            lv_obj_t *sec_lbl = lv_label_create(scroll);
            lv_label_set_text(sec_lbl, TR(TR_PARAM_OUTPUT_PORTS));
            lv_obj_set_style_text_color(sec_lbl, UI_COLOR_TEXT_DIM, 0);
            lv_obj_set_style_text_font(sec_lbl, &lv_font_montserrat_14, 0);

            for (int i = 0; i < pm_info->port_count && g_ctrl_count < MAX_CONTROLS; i++) {
                const pm_port_info_t *pm_port = &pm_info->ports[i];
                if (pm_port->type != PM_PORT_CONTROL_OUT) continue;

                ctrl_reg_t *reg = &g_controls[g_ctrl_count++];
                memset(reg, 0, sizeof(*reg));
                snprintf(reg->symbol, sizeof(reg->symbol), "%s", pm_port->symbol);
                reg->type      = CTRL_METER;
                reg->min       = pm_port->min;
                reg->max       = pm_port->max;
                reg->midi_channel = -1;
                reg->midi_cc      = -1;
                if (reg->max <= reg->min) reg->max = reg->min + 1.0f;

                /* Row: [Name 110px] [bar flex] [value 64px] */
                lv_obj_t *row = lv_obj_create(scroll);
                lv_obj_set_size(row, LV_PCT(100), 38);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_ver(row, 4, 0);
                lv_obj_set_style_pad_hor(row, 8, 0);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_column(row, 8, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                /* Port name */
                lv_obj_t *name_lbl = lv_label_create(row);
                lv_label_set_text(name_lbl,
                    pm_port->name[0] ? pm_port->name : pm_port->symbol);
                lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT, 0);
                lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
                lv_obj_set_width(name_lbl, 110);
                lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);

                /* Level bar */
                lv_obj_t *bar = lv_bar_create(row);
                lv_obj_set_height(bar, 18);
                lv_obj_set_flex_grow(bar, 1);
                lv_bar_set_range(bar, 0, 1000);
                lv_bar_set_value(bar, 0, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(bar, UI_COLOR_SURFACE, 0);
                lv_obj_set_style_bg_color(bar, UI_COLOR_ACTIVE,
                                          LV_PART_INDICATOR | LV_STATE_DEFAULT);
                lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
                reg->meter_bar = bar;

                /* Numeric value */
                lv_obj_t *val_lbl = lv_label_create(row);
                lv_label_set_text(val_lbl, "—");
                lv_obj_set_style_text_color(val_lbl, UI_COLOR_TEXT_DIM, 0);
                lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_14, 0);
                lv_obj_set_width(val_lbl, 64);
                lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, 0);
                reg->val_lbl = val_lbl;

                /* Seed with value already received (monitoring started at load) */
                float cur = 0.0f;
                if (ui_pedalboard_get_output(instance_id, pm_port->symbol, &cur))
                    ui_param_editor_update_output(pm_port->symbol, cur);
            }
        }
    }
}

void ui_param_editor_close(void)
{
    if (g_modal) {
        lv_obj_del(g_modal);
        g_modal              = NULL;
        g_instance           = -1;
        g_ctrl_count         = 0;
        g_patch_param_count  = 0;
        g_midi_chip_ud_count = 0;
        g_bypass_midi_lbl    = NULL;
        g_learning_symbol[0] = '\0';
    }
}

void ui_param_editor_update(const char *symbol, float value)
{
    ctrl_reg_t *reg = find_ctrl(symbol);
    if (!reg || !reg->widget) return;

    if (reg->type == CTRL_ARC) {
        float range = reg->max - reg->min;
        if (range > 0.0f) {
            int av = (int)(((value - reg->min) / range) * 100.0f + 0.5f);
            if (av < 0) av = 0;
            if (av > 100) av = 100;
            lv_arc_set_value(reg->widget, av);
        }
        if (reg->val_lbl) {
            char buf[32];
            if (reg->is_integer)
                snprintf(buf, sizeof(buf), "%d", (int)value);
            else
                snprintf(buf, sizeof(buf), "%.3g", (double)value);
            lv_label_set_text(reg->val_lbl, buf);
        }
    } else if (reg->type == CTRL_TOGGLE) {
        if (value >= 0.5f)
            lv_obj_add_state(reg->widget, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(reg->widget, LV_STATE_CHECKED);
    } else if (reg->type == CTRL_ENUM) {
        /* Find closest enum index */
        int sel = 0;
        for (int k = 1; k < reg->enum_count; k++)
            if (fabsf(reg->enum_values[k] - value) <
                fabsf(reg->enum_values[sel] - value))
                sel = k;
        lv_dropdown_set_selected(reg->widget, (uint16_t)sel);
    }
}

int ui_param_editor_instance(void)
{
    return g_modal ? g_instance : -1;
}

void ui_param_editor_update_output(const char *symbol, float value)
{
    ctrl_reg_t *reg = find_ctrl(symbol);
    if (!reg || reg->type != CTRL_METER || !reg->meter_bar) return;

    float range = reg->max - reg->min;
    int bar_val = 0;
    if (range > 0.0f)
        bar_val = (int)(((value - reg->min) / range) * 1000.0f + 0.5f);
    if (bar_val < 0)    bar_val = 0;
    if (bar_val > 1000) bar_val = 1000;

    lv_bar_set_value(reg->meter_bar, bar_val, LV_ANIM_OFF);

    if (reg->val_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3g", (double)value);
        lv_label_set_text(reg->val_lbl, buf);
    }
}

void ui_param_editor_on_midi_mapped(int instance_id, const char *symbol,
                                    int ch, int cc, float min, float max)
{
    if (!g_modal || g_instance != instance_id) return;

    char buf[24];
    fmt_midi(buf, sizeof(buf), ch, cc);

    if (strcmp(symbol, ":bypass") == 0) {
        g_bypass_midi_ch = ch;
        g_bypass_midi_cc = cc;
        if (g_bypass_midi_lbl) lv_label_set_text(g_bypass_midi_lbl, buf);
    } else {
        ctrl_reg_t *reg = find_ctrl(symbol);
        if (reg) {
            reg->midi_channel = ch;
            reg->midi_cc      = cc;
            if (reg->midi_lbl) lv_label_set_text(reg->midi_lbl, buf);
        }
    }

    /* Clear learning state if this symbol was being learned */
    if (strcmp(g_learning_symbol, symbol) == 0)
        g_learning_symbol[0] = '\0';

    if (g_midi_cb)
        g_midi_cb(instance_id, symbol, ch, cc, min, max, g_midi_ud);
}
