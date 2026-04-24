#include "ui_settings.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "ui_splash.h"
#include "../settings.h"
#include "../i18n.h"
#include "../host_comm.h"
#include "../pre_fx.h"
#include "../hw_detect.h"
#include "../wifi_manager.h"

#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* ─── Module state ───────────────────────────────────────────────────────────── */
static lv_obj_t *g_dd_device   = NULL;
static lv_obj_t *g_dd_buffer   = NULL;
static lv_obj_t *g_dd_bits     = NULL;

static hw_audio_device_t g_audio_devs[HW_MAX_AUDIO_DEVICES];
static int                g_n_audio    = 0;

typedef struct { int port_idx; } midi_ctx_t;
static midi_ctx_t g_midi_ctx[MPT_MAX_MIDI_PORTS];

/* ─── WiFi state ─────────────────────────────────────────────────────────────── */
typedef enum {
    WIFI_OP_NONE = 0,
    WIFI_OP_SCAN,
    WIFI_OP_CONNECT,
    WIFI_OP_HOTSPOT
} wifi_op_t;

static volatile wifi_op_t  g_wifi_op       = WIFI_OP_NONE;
static volatile int        g_wifi_result   = 0;   /* 0 = ok, -1 = fail */
static volatile bool       g_wifi_bg_done  = false; /* set by bg thread, cleared by timer */

static wifi_network_t      g_wifi_nets[WIFI_MAX_NETWORKS];
static int                 g_wifi_net_count = 0;

static lv_obj_t *g_wifi_ssid_lbl    = NULL;
static lv_obj_t *g_wifi_ip_lbl      = NULL;
static lv_obj_t *g_wifi_scan_btn    = NULL;
static lv_obj_t *g_wifi_dd_net      = NULL;
static lv_obj_t *g_wifi_pw_ta       = NULL;
static lv_obj_t *g_wifi_pw_row      = NULL;
static lv_obj_t *g_wifi_connect_btn  = NULL;
static lv_obj_t *g_wifi_hotspot_sw   = NULL;
static lv_obj_t *g_wifi_hotspot_pw   = NULL;  /* textarea for hotspot password */
static lv_obj_t *g_wifi_kbd          = NULL;
static lv_timer_t *g_wifi_poll_tmr   = NULL;

/* Shared state between bg thread and UI poll timer */
typedef struct {
    char ssid[WIFI_MAX_SSID_LEN];
    char password[128];
} wifi_connect_args_t;
static wifi_connect_args_t g_wifi_connect_args;

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

/* Background thread: wait for JACK + mod-host to come back after a restart,
 * then reconnect and reload the current pedalboard. */
typedef struct {
    char pb_path[512];
    bool pb_loaded;
} jack_restart_ctx_t;

static void *jack_restart_thread(void *arg)
{
    jack_restart_ctx_t *ctx = arg;

    /* JACK + mod-host need a few seconds to fully restart */
    sleep(6);

    if (host_comm_reconnect() != 0) {
        fprintf(stderr, "[settings] JACK restart: mod-host reconnect timed out\n");
        free(ctx);
        return NULL;
    }

    pre_fx_init();

    if (ctx->pb_loaded && ctx->pb_path[0]) {
        ui_pedalboard_load(ctx->pb_path, NULL, NULL);
    } else {
        pre_fx_reload();
    }

    free(ctx);
    return NULL;
}

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

    settings_save_prefs(s);

    /* Capture current pedalboard path before tearing down */
    jack_restart_ctx_t *ctx = calloc(1, sizeof(*ctx));
    pedalboard_t *pb = ui_pedalboard_get();
    if (pb && pb->path[0]) {
        snprintf(ctx->pb_path, sizeof(ctx->pb_path), "%s", pb->path);
        ctx->pb_loaded = true;
    }

    /* Cleanly close our JACK client before killing jackd */
    pre_fx_fini();

    settings_apply_jack(s);

    /* Reconnect in background — JACK + mod-host take several seconds to restart */
    pthread_t tid;
    pthread_create(&tid, NULL, jack_restart_thread, ctx);
    pthread_detach(tid);

    ui_app_show_toast(TR(TR_SETTINGS_JACK_RESTARTING));
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

    /* Detected JACK ports (read-only display) */
    hw_jack_ports_t jp = {0};
    char in_str[16], out_str[16];
    if (hw_detect_jack_ports(&jp) == 0 && jp.audio_capture > 0) {
        snprintf(in_str,  sizeof(in_str),  "%d", jp.audio_capture);
        snprintf(out_str, sizeof(out_str), "%d", jp.audio_playback);
    } else {
        snprintf(in_str,  sizeof(in_str),  "N/A");
        snprintf(out_str, sizeof(out_str), "N/A");
    }
    add_info_row(parent, TR(TR_SETTINGS_DETECTED_IN),  in_str);
    add_info_row(parent, TR(TR_SETTINGS_DETECTED_OUT), out_str);
    add_info_row(parent, TR(TR_SETTINGS_SAMPLE_RATE),  TR(TR_SETTINGS_SAMPLE_RATE_VAL));

    /* Dropdowns */
    g_dd_device = add_dropdown_row(parent, TR(TR_SETTINGS_INTERFACE), dev_opts,                     cur_dev_sel);
    g_dd_buffer = add_dropdown_row(parent, TR(TR_SETTINGS_BUFFER),    TR(TR_SETTINGS_BUFFER_SIZES), buf_sel);
    g_dd_bits   = add_dropdown_row(parent, TR(TR_SETTINGS_BIT_DEPTH), TR(TR_SETTINGS_BIT_DEPTH_OPTS), bits_sel);

    /* Apply button */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 260, 40);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_add_event_cb(btn, apply_audio_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, TR(TR_SETTINGS_APPLY_JACK));
    lv_obj_center(lbl);
}

/* ─── MIDI section ───────────────────────────────────────────────────────────── */

static void midi_loopback_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
    ui_pedalboard_set_midi_loopback(enabled);
}

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
        lv_label_set_text(lbl, TR(TR_SETTINGS_NO_MIDI));
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    }

    /* Merge detected ports into settings (match by capture port or label) */
    for (int i = 0; i < n_hw; i++) {
        int found = -1;
        /* Match by capture port name (primary key) */
        if (hw[i].dev[0]) {
            for (int j = 0; j < s->midi_port_count; j++)
                if (strcmp(s->midi_ports[j].dev, hw[i].dev) == 0) { found = j; break; }
        }
        /* Fallback: match by playback port for output-only devices */
        if (found < 0 && hw[i].dev_out[0]) {
            for (int j = 0; j < s->midi_port_count; j++)
                if (strcmp(s->midi_ports[j].dev_out, hw[i].dev_out) == 0) { found = j; break; }
        }
        if (found < 0 && s->midi_port_count < MPT_MAX_MIDI_PORTS) {
            found = s->midi_port_count++;
            snprintf(s->midi_ports[found].dev, sizeof(s->midi_ports[0].dev),
                     "%s", hw[i].dev);
            s->midi_ports[found].enabled = false;
        }
        if (found >= 0) {
            snprintf(s->midi_ports[found].label,   sizeof(s->midi_ports[0].label),
                     "%s", hw[i].label);
            snprintf(s->midi_ports[found].dev,     sizeof(s->midi_ports[0].dev),
                     "%s", hw[i].dev);
            snprintf(s->midi_ports[found].dev_out, sizeof(s->midi_ports[0].dev_out),
                     "%s", hw[i].dev_out);
            s->midi_ports[found].is_input  = hw[i].is_input;
            s->midi_ports[found].is_output = hw[i].is_output;
        }
    }

    for (int i = 0; i < n_hw; i++) {
        int found = -1;
        if (hw[i].dev[0]) {
            for (int j = 0; j < s->midi_port_count; j++)
                if (strcmp(s->midi_ports[j].dev, hw[i].dev) == 0) { found = j; break; }
        }
        if (found < 0 && hw[i].dev_out[0]) {
            for (int j = 0; j < s->midi_port_count; j++)
                if (strcmp(s->midi_ports[j].dev_out, hw[i].dev_out) == 0) { found = j; break; }
        }
        if (found < 0) continue;

        g_midi_ctx[i].port_idx = found;

        lv_obj_t *row = make_row(parent);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, s->midi_ports[found].label);

        if (s->midi_ports[found].enabled)
            lv_obj_add_state(cb, LV_STATE_CHECKED);

        lv_obj_set_style_text_color(cb, UI_COLOR_TEXT, 0);
        lv_obj_add_event_cb(cb, midi_toggle_cb, LV_EVENT_VALUE_CHANGED, &g_midi_ctx[i]);
    }

    /* Virtual MIDI Loopback toggle (ALSA Midi-Through → pedalboard MIDI input) */
    {
        lv_obj_t *row = make_row(parent);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, TR(TR_SETTINGS_MIDI_LOOPBACK));
        lv_obj_set_style_text_color(cb, UI_COLOR_TEXT, 0);

        /* Reflect current pedalboard state */
        if (ui_pedalboard_is_loaded()) {
            pedalboard_t *pb = ui_pedalboard_get();
            if (pb->midi_loopback)
                lv_obj_add_state(cb, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(cb, LV_STATE_DISABLED);
        }

        lv_obj_add_event_cb(cb, midi_loopback_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

/* ─── WiFi section ───────────────────────────────────────────────────────────── */

/* Rebuild the network dropdown from g_wifi_nets. */
static void wifi_rebuild_dropdown(void)
{
    if (!g_wifi_dd_net) return;
    if (g_wifi_net_count == 0) {
        lv_dropdown_set_options(g_wifi_dd_net, TR(TR_SETTINGS_WIFI_NO_CONNECTION));
        return;
    }
    char opts[WIFI_MAX_NETWORKS * (WIFI_MAX_SSID_LEN + 4)];
    opts[0] = '\0';
    for (int i = 0; i < g_wifi_net_count; i++) {
        if (i > 0) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        char entry[WIFI_MAX_SSID_LEN + 4];
        snprintf(entry, sizeof(entry), "%s%s",
                 g_wifi_nets[i].ssid,
                 g_wifi_nets[i].secured ? " *" : "");
        strncat(opts, entry, sizeof(opts) - strlen(opts) - 1);
    }
    lv_dropdown_set_options(g_wifi_dd_net, opts);
    lv_dropdown_set_selected(g_wifi_dd_net, 0);
}

/* Show/hide the password row based on whether selected network is secured. */
static void wifi_update_pw_visibility(void)
{
    if (!g_wifi_pw_row || !g_wifi_dd_net) return;
    uint16_t sel = lv_dropdown_get_selected(g_wifi_dd_net);
    bool secured = (sel < (uint16_t)g_wifi_net_count) && g_wifi_nets[sel].secured;
    if (secured)
        lv_obj_clear_flag(g_wifi_pw_row, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_wifi_pw_row, LV_OBJ_FLAG_HIDDEN);
}

/* Poll timer — checks whether the background WiFi operation has completed. */
static void wifi_poll_cb(lv_timer_t *tmr)
{
    (void)tmr;
    if (!g_wifi_bg_done) return;  /* still running */

    g_wifi_bg_done = false;
    wifi_op_t op   = g_wifi_op;
    g_wifi_op      = WIFI_OP_NONE;

    lv_timer_del(g_wifi_poll_tmr);
    g_wifi_poll_tmr = NULL;

    switch (op) {
    case WIFI_OP_SCAN:
        wifi_rebuild_dropdown();
        wifi_update_pw_visibility();
        if (g_wifi_scan_btn)
            lv_label_set_text(lv_obj_get_child(g_wifi_scan_btn, 0),
                              TR(TR_SETTINGS_WIFI_SCAN));
        break;

    case WIFI_OP_CONNECT:
        if (g_wifi_result == 0) {
            char ssid[WIFI_MAX_SSID_LEN] = "";
            char ip[32] = "";
            wifi_get_status(ssid, sizeof(ssid), ip, sizeof(ip));
            if (g_wifi_ssid_lbl)
                lv_label_set_text(g_wifi_ssid_lbl,
                                  ssid[0] ? ssid : TR(TR_SETTINGS_WIFI_NO_CONNECTION));
            if (g_wifi_ip_lbl)
                lv_label_set_text(g_wifi_ip_lbl, ip[0] ? ip : "--");
            ui_app_show_toast(TR(TR_SETTINGS_WIFI_CONNECTED_OK));
        } else {
            ui_app_show_toast(TR(TR_SETTINGS_WIFI_CONNECT_FAIL));
        }
        if (g_wifi_connect_btn)
            lv_label_set_text(lv_obj_get_child(g_wifi_connect_btn, 0),
                              TR(TR_SETTINGS_WIFI_CONNECT));
        break;

    case WIFI_OP_HOTSPOT:
        if (g_wifi_hotspot_sw) {
            bool active = wifi_hotspot_is_active();
            if (active)
                lv_obj_add_state(g_wifi_hotspot_sw, LV_STATE_CHECKED);
            else
                lv_obj_clear_state(g_wifi_hotspot_sw, LV_STATE_CHECKED);
        }
        break;

    default:
        break;
    }
}

static void *wifi_scan_thread(void *arg)
{
    (void)arg;
    g_wifi_net_count = wifi_scan(g_wifi_nets, WIFI_MAX_NETWORKS);
    g_wifi_bg_done   = true;   /* signal completion — poll timer picks it up */
    return NULL;
}

static void wifi_scan_cb(lv_event_t *e)
{
    (void)e;
    if (g_wifi_op != WIFI_OP_NONE) return;

    g_wifi_net_count = 0;
    g_wifi_op = WIFI_OP_SCAN;

    if (g_wifi_scan_btn)
        lv_label_set_text(lv_obj_get_child(g_wifi_scan_btn, 0),
                          TR(TR_SETTINGS_WIFI_SCANNING));

    pthread_t tid;
    pthread_create(&tid, NULL, wifi_scan_thread, NULL);
    pthread_detach(tid);

    if (!g_wifi_poll_tmr)
        g_wifi_poll_tmr = lv_timer_create(wifi_poll_cb, 500, NULL);
}

static void wifi_net_changed_cb(lv_event_t *e)
{
    (void)e;
    wifi_update_pw_visibility();
}

static void *wifi_connect_thread(void *arg)
{
    (void)arg;
    const char *pw = g_wifi_connect_args.password[0]
                     ? g_wifi_connect_args.password : NULL;
    g_wifi_result  = wifi_connect(g_wifi_connect_args.ssid, pw);
    g_wifi_bg_done = true;
    return NULL;
}

static void wifi_connect_cb(lv_event_t *e)
{
    (void)e;
    if (g_wifi_op != WIFI_OP_NONE || !g_wifi_dd_net) return;

    uint16_t sel = lv_dropdown_get_selected(g_wifi_dd_net);
    if (sel >= (uint16_t)g_wifi_net_count) return;

    snprintf(g_wifi_connect_args.ssid, WIFI_MAX_SSID_LEN,
             "%s", g_wifi_nets[sel].ssid);
    g_wifi_connect_args.password[0] = '\0';
    if (g_wifi_pw_ta && !lv_obj_has_flag(g_wifi_pw_row, LV_OBJ_FLAG_HIDDEN))
        snprintf(g_wifi_connect_args.password,
                 sizeof(g_wifi_connect_args.password),
                 "%s", lv_textarea_get_text(g_wifi_pw_ta));

    g_wifi_op = WIFI_OP_CONNECT;
    if (g_wifi_connect_btn)
        lv_label_set_text(lv_obj_get_child(g_wifi_connect_btn, 0),
                          TR(TR_SETTINGS_WIFI_CONNECTING));

    pthread_t tid;
    pthread_create(&tid, NULL, wifi_connect_thread, NULL);
    pthread_detach(tid);

    if (!g_wifi_poll_tmr)
        g_wifi_poll_tmr = lv_timer_create(wifi_poll_cb, 500, NULL);
}

static void *wifi_hotspot_thread(void *arg)
{
    bool enable = (bool)(uintptr_t)arg;
    mpt_settings_t *s = settings_get();
    wifi_hotspot_set(enable, enable ? s->hotspot_password : NULL);

    s->hotspot_enabled = wifi_hotspot_is_active();
    settings_save_prefs(s);

    g_wifi_bg_done = true;
    return NULL;
}

static void wifi_hotspot_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enable  = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (g_wifi_op != WIFI_OP_NONE) return;

    g_wifi_op = WIFI_OP_HOTSPOT;
    pthread_t tid;
    pthread_create(&tid, NULL, wifi_hotspot_thread, (void *)(uintptr_t)enable);
    pthread_detach(tid);

    if (!g_wifi_poll_tmr)
        g_wifi_poll_tmr = lv_timer_create(wifi_poll_cb, 500, NULL);
}

/* Password textarea — show an LVGL keyboard when focused. */
static void wifi_pw_ta_focused_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *parent = lv_obj_get_parent(lv_obj_get_parent(ta)); /* grandparent = screen */
    if (!parent) return;

    if (!g_wifi_kbd) {
        g_wifi_kbd = lv_keyboard_create(lv_scr_act());
        lv_obj_set_size(g_wifi_kbd, LV_PCT(100), LV_PCT(45));
        lv_obj_align(g_wifi_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
        ui_app_keyboard_apply_lang(g_wifi_kbd);
    }
    lv_keyboard_set_textarea(g_wifi_kbd, ta);
    lv_obj_clear_flag(g_wifi_kbd, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_pw_ta_defocused_cb(lv_event_t *e)
{
    (void)e;
    if (g_wifi_kbd) lv_obj_add_flag(g_wifi_kbd, LV_OBJ_FLAG_HIDDEN);
}

/* ─── Power section ──────────────────────────────────────────────────────────── */

/* Generic confirmation modal: message + Confirm/Cancel buttons.
 * confirm_cb is called with user_data when the user taps Confirm. */
static void power_confirm_close_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_del(overlay);
}

typedef struct { lv_event_cb_t action_cb; void *user_data; } power_confirm_ctx_t;
/* Max 4 concurrent confirm dialogs — unlikely but safe. */
static power_confirm_ctx_t g_pcc[4];
static int                 g_pcc_idx = 0;

static void power_confirm_ok_cb(lv_event_t *e)
{
    power_confirm_ctx_t *ctx = lv_event_get_user_data(e);
    /* Close the overlay (grandparent of the button) */
    lv_obj_t *btn     = lv_event_get_target(e);
    lv_obj_t *card    = lv_obj_get_parent(btn);
    lv_obj_t *overlay = lv_obj_get_parent(card);
    lv_obj_del(overlay);
    /* Execute the action */
    if (ctx->action_cb) ctx->action_cb(NULL);
}

static void show_confirm(const char *msg,
                         lv_event_cb_t confirm_cb, void *user_data)
{
    power_confirm_ctx_t *ctx = &g_pcc[g_pcc_idx++ % 4];
    ctx->action_cb  = confirm_cb;
    ctx->user_data  = user_data;

    /* Semi-transparent full-screen overlay */
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Centered card */
    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, 480, 160);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Message */
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

    /* Button row */
    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 24, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    /* Cancel */
    lv_obj_t *btn_cancel = lv_btn_create(btn_row);
    lv_obj_set_size(btn_cancel, 160, 44);
    lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_TEXT_DIM, 0);
    lv_obj_add_event_cb(btn_cancel, power_confirm_close_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, TR(TR_CANCEL));
    lv_obj_center(lbl_cancel);

    /* Confirm */
    lv_obj_t *btn_ok = lv_btn_create(btn_row);
    lv_obj_set_size(btn_ok, 160, 44);
    lv_obj_set_style_bg_color(btn_ok, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(btn_ok, power_confirm_ok_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, TR(TR_OK));
    lv_obj_center(lbl_ok);
}

static void do_shutdown(lv_event_t *e)
{
    (void)e;
    ui_splash_show_power(0);   /* show "Arrêt du système..." then flush fb */
    system("sudo /bin/systemctl poweroff");
}

static void do_reboot(lv_event_t *e)
{
    (void)e;
    ui_splash_show_power(1);   /* show "Redémarrage..." then flush fb */
    system("sudo /bin/systemctl reboot");
}

static void shutdown_btn_cb(lv_event_t *e)
{
    (void)e;
    show_confirm(TR(TR_SETTINGS_CONFIRM_SHUTDOWN), do_shutdown, NULL);
}

static void reboot_btn_cb(lv_event_t *e)
{
    (void)e;
    show_confirm(TR(TR_SETTINGS_CONFIRM_REBOOT), do_reboot, NULL);
}

/* ─── MOD-UI section ─────────────────────────────────────────────────────────── */

static void modui_toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    mpt_settings_t *s = settings_get();
    bool activate = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (activate) {
        /* Start mod-ui.service, then release mod-host connection */
        system("sudo systemctl start mod-ui");
        host_comm_disconnect();
        s->mod_ui_active = true;
    } else {
        /* Stop mod-ui.service, then reconnect to mod-host */
        system("sudo systemctl stop mod-ui");
        s->mod_ui_active = false;
        host_comm_reconnect();
    }
    /* Navigate to pedalboard — shows placeholder or reloads depending on state */
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

static void build_modui_section(lv_obj_t *parent)
{
    lv_obj_t *row = make_row(parent);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, TR(TR_SETTINGS_MODUI_ACTIVATE));
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t *sw = lv_switch_create(row);
    if (settings_get()->mod_ui_active)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, modui_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_power_section(lv_obj_t *parent)
{
    lv_obj_t *row = make_row(parent);

    lv_obj_t *btn_shutdown = lv_btn_create(row);
    lv_obj_set_size(btn_shutdown, 200, 44);
    lv_obj_set_style_bg_color(btn_shutdown, lv_palette_darken(LV_PALETTE_RED, 1), 0);
    lv_obj_add_event_cb(btn_shutdown, shutdown_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_sd = lv_label_create(btn_shutdown);
    lv_label_set_text_fmt(lbl_sd, LV_SYMBOL_POWER " %s", TR(TR_SETTINGS_SHUTDOWN));
    lv_obj_center(lbl_sd);

    lv_obj_t *btn_reboot = lv_btn_create(row);
    lv_obj_set_size(btn_reboot, 200, 44);
    lv_obj_set_style_bg_color(btn_reboot, lv_palette_darken(LV_PALETTE_ORANGE, 1), 0);
    lv_obj_add_event_cb(btn_reboot, reboot_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_rb = lv_label_create(btn_reboot);
    lv_label_set_text_fmt(lbl_rb, LV_SYMBOL_REFRESH " %s", TR(TR_SETTINGS_REBOOT));
    lv_obj_center(lbl_rb);
}

/* Save hotspot password when the textarea loses focus. */
static void wifi_hotspot_pw_save_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    const char *pw = lv_textarea_get_text(ta);
    if (!pw || strlen(pw) < 8) return;   /* WPA2 minimum 8 chars */

    mpt_settings_t *s = settings_get();
    snprintf(s->hotspot_password, sizeof(s->hotspot_password), "%s", pw);
    settings_save_prefs(s);
    ui_app_show_toast(TR(TR_SETTINGS_WIFI_HOTSPOT_PW_SAVED));
}

static void build_wifi_section(lv_obj_t *parent)
{
    /* ── Current network status ── */
    char ssid[WIFI_MAX_SSID_LEN] = "";
    char ip[32] = "";
    wifi_get_status(ssid, sizeof(ssid), ip, sizeof(ip));

    /* SSID row */
    g_wifi_ssid_lbl = add_info_row(parent,
                                   TR(TR_SETTINGS_WIFI_CURRENT),
                                   ssid[0] ? ssid : TR(TR_SETTINGS_WIFI_NO_CONNECTION));

    /* IP row */
    g_wifi_ip_lbl = add_info_row(parent,
                                 TR(TR_SETTINGS_WIFI_IP),
                                 ip[0] ? ip : "--");

    /* ── Scan button ── */
    lv_obj_t *scan_row = make_row(parent);
    g_wifi_scan_btn = lv_btn_create(scan_row);
    lv_obj_set_size(g_wifi_scan_btn, 260, 40);
    lv_obj_set_style_bg_color(g_wifi_scan_btn, UI_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(g_wifi_scan_btn, wifi_scan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(g_wifi_scan_btn);
    lv_label_set_text(scan_lbl, TR(TR_SETTINGS_WIFI_SCAN));
    lv_obj_center(scan_lbl);

    /* ── Network dropdown ── */
    g_wifi_dd_net = add_dropdown_row(parent, TR(TR_SETTINGS_WIFI_NETWORK),
                                     TR(TR_SETTINGS_WIFI_NO_CONNECTION), 0);
    lv_obj_add_event_cb(g_wifi_dd_net, wifi_net_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Password row (hidden by default) ── */
    g_wifi_pw_row = make_row(parent);
    lv_obj_add_flag(g_wifi_pw_row, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *pw_key = lv_label_create(g_wifi_pw_row);
    lv_label_set_text(pw_key, TR(TR_SETTINGS_WIFI_PASSWORD));
    lv_obj_set_style_text_color(pw_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_flex_grow(pw_key, 1);

    g_wifi_pw_ta = lv_textarea_create(g_wifi_pw_row);
    lv_obj_set_width(g_wifi_pw_ta, 320);
    lv_obj_set_height(g_wifi_pw_ta, 40);
    lv_textarea_set_one_line(g_wifi_pw_ta, true);
    lv_textarea_set_password_mode(g_wifi_pw_ta, true);
    lv_textarea_set_placeholder_text(g_wifi_pw_ta, "••••••••");
    lv_obj_set_style_bg_color(g_wifi_pw_ta, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(g_wifi_pw_ta, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_color(g_wifi_pw_ta, UI_COLOR_TEXT_DIM, 0);
    lv_obj_add_event_cb(g_wifi_pw_ta, wifi_pw_ta_focused_cb,   LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(g_wifi_pw_ta, wifi_pw_ta_defocused_cb, LV_EVENT_DEFOCUSED, NULL);

    /* ── Connect button ── */
    lv_obj_t *con_row = make_row(parent);
    g_wifi_connect_btn = lv_btn_create(con_row);
    lv_obj_set_size(g_wifi_connect_btn, 260, 40);
    lv_obj_set_style_bg_color(g_wifi_connect_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_add_event_cb(g_wifi_connect_btn, wifi_connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *con_lbl = lv_label_create(g_wifi_connect_btn);
    lv_label_set_text(con_lbl, TR(TR_SETTINGS_WIFI_CONNECT));
    lv_obj_center(con_lbl);

    /* ── Hotspot ── */
    lv_obj_t *hs_row = make_row(parent);
    lv_obj_t *hs_key = lv_label_create(hs_row);
    lv_label_set_text(hs_key, TR(TR_SETTINGS_WIFI_HOTSPOT));
    lv_obj_set_style_text_color(hs_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_flex_grow(hs_key, 1);

    /* SSID info */
    lv_obj_t *hs_ssid = lv_label_create(hs_row);
    lv_label_set_text(hs_ssid, TR(TR_SETTINGS_WIFI_HOTSPOT_SSID));
    lv_obj_set_style_text_color(hs_ssid, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_flex_grow(hs_ssid, 2);

    g_wifi_hotspot_sw = lv_switch_create(hs_row);
    if (wifi_hotspot_is_active())
        lv_obj_add_state(g_wifi_hotspot_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_wifi_hotspot_sw, wifi_hotspot_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Hotspot password ── */
    lv_obj_t *hpw_row = make_row(parent);
    lv_obj_t *hpw_key = lv_label_create(hpw_row);
    lv_label_set_text(hpw_key, TR(TR_SETTINGS_WIFI_HOTSPOT_PASSWORD));
    lv_obj_set_style_text_color(hpw_key, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_flex_grow(hpw_key, 1);

    g_wifi_hotspot_pw = lv_textarea_create(hpw_row);
    lv_obj_set_width(g_wifi_hotspot_pw, 320);
    lv_obj_set_height(g_wifi_hotspot_pw, 40);
    lv_textarea_set_one_line(g_wifi_hotspot_pw, true);
    lv_textarea_set_password_mode(g_wifi_hotspot_pw, true);
    lv_obj_set_style_bg_color(g_wifi_hotspot_pw, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(g_wifi_hotspot_pw, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_color(g_wifi_hotspot_pw, UI_COLOR_TEXT_DIM, 0);

    /* Pre-fill with saved password */
    mpt_settings_t *hs = settings_get();
    lv_textarea_set_text(g_wifi_hotspot_pw,
                         hs->hotspot_password[0] ? hs->hotspot_password
                                                  : WIFI_HOTSPOT_PASSWORD_DEF);

    lv_obj_add_event_cb(g_wifi_hotspot_pw, wifi_pw_ta_focused_cb,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(g_wifi_hotspot_pw, wifi_pw_ta_defocused_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(g_wifi_hotspot_pw, wifi_hotspot_pw_save_cb, LV_EVENT_DEFOCUSED, NULL);
}

/* ─── Language change ────────────────────────────────────────────────────────── */

static void lang_changed_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    mpt_lang_t lang = (sel < LANG_COUNT) ? (mpt_lang_t)sel : LANG_EN;

    i18n_set_lang(lang);

    mpt_settings_t *s = settings_get();
    snprintf(s->language, sizeof(s->language), "%s", i18n_lang_code(lang));
    settings_save_prefs(s);

    /* Rebuild the whole UI with new language */
    ui_app_apply_language();
}

/* ─── Main entry point ───────────────────────────────────────────────────────── */

void ui_settings_show(lv_obj_t *parent)
{
    mpt_settings_t *s = settings_get();

    g_dd_device = g_dd_buffer = g_dd_bits = NULL;

    /* WiFi pointers — reset each time the screen is rebuilt */
    g_wifi_ssid_lbl    = NULL;
    g_wifi_ip_lbl      = NULL;
    g_wifi_scan_btn    = NULL;
    g_wifi_dd_net      = NULL;
    g_wifi_pw_ta       = NULL;
    g_wifi_pw_row      = NULL;
    g_wifi_connect_btn  = NULL;
    g_wifi_hotspot_sw   = NULL;
    g_wifi_hotspot_pw   = NULL;
    g_wifi_kbd          = NULL;
    /* Do NOT reset g_wifi_poll_tmr here — a background op may still be running */

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

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, TR(TR_SETTINGS_TITLE));
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);

    /* ── System ── */
    add_section_header(parent, TR(TR_SETTINGS_SYSTEM));
    add_info_row(parent, TR(TR_SETTINGS_HOST_ADDR), s->host_addr);
    {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", s->host_cmd_port);
        add_info_row(parent, TR(TR_SETTINGS_HOST_PORT), port_str);
    }
    add_info_row(parent, TR(TR_SETTINGS_PB_DIR),  s->pedalboards_dir);
    add_info_row(parent, TR(TR_SETTINGS_FB),       s->fb_device);
    add_info_row(parent, TR(TR_SETTINGS_TOUCH),    s->touch_device);
    add_info_row(parent, TR(TR_SETTINGS_MOD_HOST),
        host_comm_is_connected() ? TR(TR_SETTINGS_CONNECTED)
                                 : TR(TR_SETTINGS_DISCONNECTED));

    {
        lv_obj_t *cpu_row = make_row(parent);
        lv_obj_t *cpu_key = lv_label_create(cpu_row);
        lv_label_set_text(cpu_key, TR(TR_SETTINGS_CPU));
        lv_obj_set_style_text_color(cpu_key, UI_COLOR_TEXT_DIM, 0);
        lv_obj_t *cpu_val = lv_label_create(cpu_row);
        lv_label_set_text(cpu_val, TR(TR_SETTINGS_CPU_DEFAULT));
        lv_obj_set_style_text_color(cpu_val, UI_COLOR_TEXT, 0);
        lv_obj_t *cpu_btn = lv_btn_create(cpu_row);
        lv_obj_set_size(cpu_btn, 44, 32);
        lv_obj_set_style_bg_color(cpu_btn, UI_COLOR_ACCENT, 0);
        lv_obj_t *lbl_r = lv_label_create(cpu_btn);
        lv_label_set_text(lbl_r, LV_SYMBOL_REFRESH);
        lv_obj_center(lbl_r);
        lv_obj_add_event_cb(cpu_btn, cpu_refresh_cb, LV_EVENT_CLICKED, cpu_val);
    }

    /* ── Language ── */
    {
        lv_obj_t *dd_lang = add_dropdown_row(parent, TR(TR_SETTINGS_LANGUAGE),
                                              TR(TR_SETTINGS_LANGUAGE_OPTS),
                                              (int)i18n_get_lang());
        lv_obj_add_event_cb(dd_lang, lang_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* ── Audio ── */
    add_section_header(parent, TR(TR_SETTINGS_AUDIO));
    build_audio_section(parent);

    /* ── MIDI ── */
    add_section_header(parent, TR(TR_SETTINGS_MIDI));
    build_midi_section(parent);

    /* ── WiFi ── */
    add_section_header(parent, TR(TR_SETTINGS_WIFI));
    build_wifi_section(parent);

    /* ── MOD-UI ── */
    add_section_header(parent, TR(TR_SETTINGS_MODUI));
    build_modui_section(parent);

    /* ── Power ── */
    add_section_header(parent, TR(TR_SETTINGS_POWER));
    build_power_section(parent);
}
