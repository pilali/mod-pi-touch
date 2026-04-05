#include "ui_settings.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../settings.h"
#include "../host_comm.h"
#include "../hw_detect.h"

#include <string.h>
#include <stdio.h>

/* ─── Module state ───────────────────────────────────────────────────────────── */
static lv_obj_t *g_dd_device   = NULL;
static lv_obj_t *g_dd_buffer   = NULL;
static lv_obj_t *g_dd_bits     = NULL;
static lv_obj_t *g_dd_in_ch    = NULL;
static lv_obj_t *g_dd_out_ch   = NULL;

static hw_audio_device_t g_audio_devs[HW_MAX_AUDIO_DEVICES];
static int                g_n_audio    = 0;

typedef struct { int port_idx; } midi_ctx_t;
static midi_ctx_t g_midi_ctx[MPT_MAX_MIDI_PORTS];

/* ─── Helpers ─────────────────────────────────────────────────────────────────── */

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

static void cpu_refresh_cb(lv_event_t *e)
{
    lv_obj_t *lbl = lv_event_get_user_data(e);
    float cpu = 0.0f;
    host_cpu_load(&cpu);
    char buf[64];
    snprintf(buf, sizeof(buf), "CPU: %.1f%%", (double)cpu);
    lv_label_set_text(lbl, buf);
}

static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *add_info_row(lv_obj_t *parent, const char *label, const char *value)
{
    lv_obj_t *row = make_row(parent);
    lv_obj_t *lbl_w = lv_label_create(row);
    lv_label_set_text(lbl_w, label);
    lv_obj_set_style_text_color(lbl_w, UI_COLOR_TEXT_DIM, 0);
    lv_obj_t *val_w = lv_label_create(row);
    lv_label_set_text(val_w, value);
    lv_obj_set_style_text_color(val_w, UI_COLOR_TEXT, 0);
    return val_w;
}

static void add_section_header(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
}

static lv_obj_t *add_dropdown_row(lv_obj_t *parent, const char *label,
                                   const char *options, int sel)
{
    lv_obj_t *row = make_row(parent);

    lv_obj_t *lbl_w = lv_label_create(row);
    lv_label_set_text(lbl_w, label);
    lv_obj_set_style_text_color(lbl_w, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_flex_grow(lbl_w, 1);

    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, (uint16_t)sel);
    lv_obj_set_width(dd, 300);
    lv_obj_set_style_bg_color(dd, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(dd, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_color(dd, UI_COLOR_TEXT, 0);
    return dd;
}

/* ─── Audio section ──────────────────────────────────────────────────────────── */

static void apply_audio_cb(lv_event_t *e)
{
    (void)e;
    if (!g_dd_device || !g_dd_buffer || !g_dd_bits) return;

    mpt_settings_t *s = settings_get();

    /* Device */
    uint16_t dev_sel = lv_dropdown_get_selected(g_dd_device);
    if (dev_sel < (uint16_t)g_n_audio)
        snprintf(s->jack_audio_device, sizeof(s->jack_audio_device),
                 "%s", g_audio_devs[dev_sel].alsa_id);

    /* Buffer size */
    static const int buf_sizes[] = {32, 64, 128, 256};
    uint16_t buf_sel = lv_dropdown_get_selected(g_dd_buffer);
    s->jack_buffer_size = buf_sizes[buf_sel < 4 ? buf_sel : 2];

    /* Bit depth */
    s->jack_bit_depth = (lv_dropdown_get_selected(g_dd_bits) == 0) ? 16 : 24;

    /* Channel counts: index maps to 1, 2, 4, 6, 8 */
    static const int ch_vals[] = {1, 2, 4, 6, 8};
    uint16_t ich = lv_dropdown_get_selected(g_dd_in_ch);
    uint16_t och = lv_dropdown_get_selected(g_dd_out_ch);
    s->audio_capture_ch  = ch_vals[ich < 5 ? ich : 1];
    s->audio_playback_ch = ch_vals[och < 5 ? och : 1];

    settings_save_prefs(s);
    settings_apply_jack(s);

    /* Refresh pedalboard display with new channel counts */
    if (ui_pedalboard_is_loaded())
        ui_pedalboard_refresh();

    ui_app_show_message("Audio",
        "JACK restarting.\nReload pedalboard if needed.", 0);
}

static void build_audio_section(lv_obj_t *parent)
{
    mpt_settings_t *s = settings_get();

    g_n_audio = hw_detect_audio(g_audio_devs, HW_MAX_AUDIO_DEVICES);

    /* Device options string */
    char dev_opts[HW_MAX_AUDIO_DEVICES * 80];
    dev_opts[0] = '\0';
    int cur_dev_sel = 0;
    for (int i = 0; i < g_n_audio; i++) {
        if (i > 0) strncat(dev_opts, "\n", sizeof(dev_opts) - strlen(dev_opts) - 1);
        char entry[80];
        snprintf(entry, sizeof(entry), "%s - %s",
                 g_audio_devs[i].alsa_id, g_audio_devs[i].label);
        strncat(dev_opts, entry, sizeof(dev_opts) - strlen(dev_opts) - 1);
        if (strcmp(g_audio_devs[i].alsa_id, s->jack_audio_device) == 0)
            cur_dev_sel = i;
    }
    if (g_n_audio == 0)
        snprintf(dev_opts, sizeof(dev_opts), "%s", s->jack_audio_device);

    /* Buffer size */
    static const int buf_sizes[] = {32, 64, 128, 256};
    int buf_sel = 2;
    for (int i = 0; i < 4; i++)
        if (buf_sizes[i] == s->jack_buffer_size) { buf_sel = i; break; }

    /* Bit depth */
    int bits_sel = (s->jack_bit_depth == 16) ? 0 : 1;

    /* Channel count: map value to index in {1,2,4,6,8} */
    static const int ch_vals[] = {1, 2, 4, 6, 8};
    int in_ch_sel = 1, out_ch_sel = 1; /* default: 2 channels */
    for (int i = 0; i < 5; i++) {
        if (ch_vals[i] == s->audio_capture_ch)  in_ch_sel  = i;
        if (ch_vals[i] == s->audio_playback_ch) out_ch_sel = i;
    }

    /* Dropdowns */
    g_dd_device  = add_dropdown_row(parent, "Interface",   dev_opts,           cur_dev_sel);
    g_dd_buffer  = add_dropdown_row(parent, "Buffer",      "32\n64\n128\n256", buf_sel);
    g_dd_bits    = add_dropdown_row(parent, "Bit depth",   "16 bit\n24 bit",   bits_sel);
    g_dd_in_ch   = add_dropdown_row(parent, "In channels", "1\n2\n4\n6\n8",    in_ch_sel);
    g_dd_out_ch  = add_dropdown_row(parent, "Out channels","1\n2\n4\n6\n8",    out_ch_sel);
    add_info_row(parent, "Sample rate", "48000 Hz (fixed)");

    /* Apply button */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 220, 40);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_add_event_cb(btn, apply_audio_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Apply (restart JACK)");
    lv_obj_center(lbl);
}

/* ─── MIDI section ───────────────────────────────────────────────────────────── */

static void midi_toggle_cb(lv_event_t *e)
{
    midi_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t   *cb  = lv_event_get_target(e);
    mpt_settings_t *s = settings_get();

    if (ctx->port_idx >= 0 && ctx->port_idx < s->midi_port_count)
        s->midi_ports[ctx->port_idx].enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);

    settings_save_prefs(s);

    if (ui_pedalboard_is_loaded())
        ui_pedalboard_refresh();
}

static void build_midi_section(lv_obj_t *parent)
{
    mpt_settings_t *s = settings_get();

    hw_midi_port_t hw[HW_MAX_MIDI_PORTS];
    int n_hw = hw_detect_midi(hw, HW_MAX_MIDI_PORTS);

    if (n_hw == 0) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, "No MIDI devices detected.");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
        return;
    }

    /* Merge detected ports into settings */
    for (int i = 0; i < n_hw; i++) {
        int found = -1;
        for (int j = 0; j < s->midi_port_count; j++)
            if (strcmp(s->midi_ports[j].dev, hw[i].dev) == 0) { found = j; break; }
        if (found < 0 && s->midi_port_count < MPT_MAX_MIDI_PORTS) {
            found = s->midi_port_count++;
            snprintf(s->midi_ports[found].dev, sizeof(s->midi_ports[0].dev),
                     "%s", hw[i].dev);
            s->midi_ports[found].enabled = false;
        }
        if (found >= 0) {
            snprintf(s->midi_ports[found].label, sizeof(s->midi_ports[0].label),
                     "%s", hw[i].label);
            s->midi_ports[found].is_input  = hw[i].is_input;
            s->midi_ports[found].is_output = hw[i].is_output;
        }
    }

    for (int i = 0; i < n_hw; i++) {
        int found = -1;
        for (int j = 0; j < s->midi_port_count; j++)
            if (strcmp(s->midi_ports[j].dev, hw[i].dev) == 0) { found = j; break; }
        if (found < 0) continue;

        g_midi_ctx[i].port_idx = found;

        lv_obj_t *row = make_row(parent);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *cb = lv_checkbox_create(row);

        char dir[8] = "";
        if (s->midi_ports[found].is_input && s->midi_ports[found].is_output)
            snprintf(dir, sizeof(dir), " [I/O]");
        else if (s->midi_ports[found].is_input)
            snprintf(dir, sizeof(dir), " [In]");
        else if (s->midi_ports[found].is_output)
            snprintf(dir, sizeof(dir), " [Out]");

        char cb_text[128];
        snprintf(cb_text, sizeof(cb_text), "%s%s",
                 s->midi_ports[found].label, dir);
        lv_checkbox_set_text(cb, cb_text);

        if (s->midi_ports[found].enabled)
            lv_obj_add_state(cb, LV_STATE_CHECKED);

        lv_obj_set_style_text_color(cb, UI_COLOR_TEXT, 0);
        lv_obj_add_event_cb(cb, midi_toggle_cb, LV_EVENT_VALUE_CHANGED, &g_midi_ctx[i]);
    }
}

/* ─── Main entry point ───────────────────────────────────────────────────────── */

void ui_settings_show(lv_obj_t *parent)
{
    mpt_settings_t *s = settings_get();

    g_dd_device = g_dd_buffer = g_dd_bits = NULL;
    g_dd_in_ch  = g_dd_out_ch = NULL;

    lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 10, 0);

    /* ── Header ── */
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 44);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_pad_column(hdr, 8, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_set_style_bg_color(btn_back, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, "Settings");
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);

    /* ── System ── */
    add_section_header(parent, "System");
    add_info_row(parent, "mod-host address", s->host_addr);
    {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", s->host_cmd_port);
        add_info_row(parent, "mod-host port", port_str);
    }
    add_info_row(parent, "Pedalboards", s->pedalboards_dir);
    add_info_row(parent, "Framebuffer",  s->fb_device);
    add_info_row(parent, "Touch",        s->touch_device);
    add_info_row(parent, "mod-host",
        host_comm_is_connected() ? "Connected" : "Disconnected");

    {
        lv_obj_t *cpu_row = make_row(parent);
        lv_obj_t *cpu_key = lv_label_create(cpu_row);
        lv_label_set_text(cpu_key, "CPU load");
        lv_obj_set_style_text_color(cpu_key, UI_COLOR_TEXT_DIM, 0);
        lv_obj_t *cpu_val = lv_label_create(cpu_row);
        lv_label_set_text(cpu_val, "-- %");
        lv_obj_set_style_text_color(cpu_val, UI_COLOR_TEXT, 0);
        lv_obj_t *cpu_btn = lv_btn_create(cpu_row);
        lv_obj_set_size(cpu_btn, 44, 32);
        lv_obj_set_style_bg_color(cpu_btn, UI_COLOR_ACCENT, 0);
        lv_obj_t *lbl_r = lv_label_create(cpu_btn);
        lv_label_set_text(lbl_r, LV_SYMBOL_REFRESH);
        lv_obj_center(lbl_r);
        lv_obj_add_event_cb(cpu_btn, cpu_refresh_cb, LV_EVENT_CLICKED, cpu_val);
    }

    /* ── Audio ── */
    add_section_header(parent, "Audio");
    build_audio_section(parent);

    /* ── MIDI ── */
    add_section_header(parent, "MIDI");
    build_midi_section(parent);
}
