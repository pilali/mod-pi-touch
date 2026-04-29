#include "ui_conductor.h"
#include "ui_app.h"
#include "../pedalboard.h"
#include "../host_comm.h"
#include "../plugin_manager.h"
#include "../i18n.h"
#include "ui_pedalboard.h"

#include <lvgl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ─── State ──────────────────────────────────────────────────────────────────── */

static lv_obj_t *g_overlay       = NULL;
static lv_obj_t *g_bpm_label     = NULL;
static lv_obj_t *g_bpb_label     = NULL;
static lv_obj_t *g_bpb_denom_label = NULL;
static lv_obj_t *g_btn_play      = NULL;
static lv_obj_t *g_btn_stop      = NULL;
static lv_obj_t *g_btn_int       = NULL;   /* Internal clock toggle */
static lv_obj_t *g_btn_midi      = NULL;   /* MIDI slave toggle */

static int g_bpb_denom = 4;   /* time signature denominator (2/4/8/16) */

/* Tap tempo: up to 8 taps, compute average interval */
#define TAP_MAX 8
static struct timespec g_taps[TAP_MAX];
static int             g_tap_count = 0;

/* ─── Compute tempo-synced port value from BPM + divider + unit ──────────────── */

static float tempo_compute_value(float bpm, float divider, const char *unit)
{
    if (divider <= 0.0f) return 0.0f;
    if (strcmp(unit, "BPM") == 0) return bpm / divider;
    float secs = 240.0f / (bpm * divider);
    if (strcmp(unit, "ms")  == 0) return secs * 1000.0f;
    if (strcmp(unit, "min") == 0) return secs / 60.0f;
    if (strcmp(unit, "Hz")  == 0) return (secs > 0.0f) ? 1.0f   / secs : 0.0f;
    if (strcmp(unit, "kHz") == 0) return (secs > 0.0f) ? 0.001f / secs : 0.0f;
    if (strcmp(unit, "MHz") == 0) return (secs > 0.0f) ? 0.000001f / secs : 0.0f;
    return secs; /* "s" */
}

/* Resend all tempo-synced parameters after a BPM change. */
static void apply_tempo_params(pedalboard_t *pb)
{
    for (int i = 0; i < pb->plugin_count; i++) {
        pb_plugin_t *plug = &pb->plugins[i];
        const pm_plugin_info_t *pm = pm_plugin_by_uri(plug->uri);
        for (int j = 0; j < plug->port_count; j++) {
            pb_port_t *port = &plug->ports[j];
            if (port->tempo_divider <= 0.0f) continue;

            const char *unit = "";
            if (pm) {
                for (int k = 0; k < pm->port_count; k++) {
                    if (strcmp(pm->ports[k].symbol, port->symbol) == 0) {
                        unit = pm->ports[k].unit_symbol;
                        break;
                    }
                }
            }
            float val = tempo_compute_value(pb->bpm, port->tempo_divider, unit);
            /* Clamp to port range */
            if (val < port->min) val = port->min;
            if (val > port->max) val = port->max;
            port->value = val;
            host_param_set(plug->instance_id, port->symbol, val);
        }
    }
}

/* ─── Helper: apply transport to mod-host and mark pedalboard modified ───────── */

static void apply_transport(void)
{
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    host_transport(pb->transport_rolling, pb->bpb, pb->bpm);
    apply_tempo_params(pb);
    pb->modified = true;
    ui_app_update_title(pb->name, true);
}

/* ─── BPM label refresh ──────────────────────────────────────────────────────── */

static void refresh_bpm_label(float bpm)
{
    if (!g_bpm_label) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", bpm);
    lv_label_set_text(g_bpm_label, buf);
}

static void refresh_bpb_label(float bpb)
{
    if (!g_bpb_label) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f / %d", bpb, g_bpb_denom);
    lv_label_set_text(g_bpb_label, buf);
}

static void refresh_bpb_denom_label(void)
{
    if (!g_bpb_denom_label) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_bpb_denom);
    lv_label_set_text(g_bpb_denom_label, buf);
}

/* ─── Highlight play/stop buttons depending on rolling state ─────────────────── */

static void refresh_rolling_buttons(bool rolling)
{
    if (!g_btn_play || !g_btn_stop) return;
    lv_color_t active  = UI_COLOR_ACTIVE;
    lv_color_t surface = UI_COLOR_SURFACE;
    lv_obj_set_style_bg_color(g_btn_play, rolling ? active  : surface, 0);
    lv_obj_set_style_bg_color(g_btn_stop, rolling ? surface : lv_color_hex(0xC0392B), 0);
}

static void refresh_sync_buttons(int sync)
{
    if (!g_btn_int || !g_btn_midi) return;
    lv_color_t accent  = UI_COLOR_ACCENT;
    lv_color_t surface = UI_COLOR_SURFACE;
    lv_obj_set_style_bg_color(g_btn_int,  sync == 0 ? accent : surface, 0);
    lv_obj_set_style_bg_color(g_btn_midi, sync == 1 ? accent : surface, 0);
}

/* ─── Button callbacks ───────────────────────────────────────────────────────── */

static void bpm_minus_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->bpm -= 1.0f;
    if (pb->bpm < 20.0f) pb->bpm = 20.0f;
    refresh_bpm_label(pb->bpm);
    apply_transport();
    g_tap_count = 0;
}

static void bpm_plus_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->bpm += 1.0f;
    if (pb->bpm > 280.0f) pb->bpm = 280.0f;
    refresh_bpm_label(pb->bpm);
    apply_transport();
    g_tap_count = 0;
}

static void bpm_minus10_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->bpm -= 10.0f;
    if (pb->bpm < 20.0f) pb->bpm = 20.0f;
    refresh_bpm_label(pb->bpm);
    apply_transport();
    g_tap_count = 0;
}

static void bpm_plus10_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->bpm += 10.0f;
    if (pb->bpm > 280.0f) pb->bpm = 280.0f;
    refresh_bpm_label(pb->bpm);
    apply_transport();
    g_tap_count = 0;
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (g_tap_count > 0) {
        /* Check if the last tap was more than 3 s ago → reset */
        struct timespec *prev = &g_taps[(g_tap_count - 1) % TAP_MAX];
        long dt_ms = (now.tv_sec  - prev->tv_sec)  * 1000
                   + (now.tv_nsec - prev->tv_nsec) / 1000000;
        if (dt_ms > 3000) g_tap_count = 0;
    }

    g_taps[g_tap_count % TAP_MAX] = now;
    g_tap_count++;

    if (g_tap_count >= 2) {
        /* Average interval over up to TAP_MAX taps */
        int n = g_tap_count < TAP_MAX ? g_tap_count : TAP_MAX;
        struct timespec *first = &g_taps[(g_tap_count - n) % TAP_MAX];
        struct timespec *last  = &g_taps[(g_tap_count - 1) % TAP_MAX];
        long total_ms = (last->tv_sec  - first->tv_sec)  * 1000
                      + (last->tv_nsec - first->tv_nsec) / 1000000;
        if (total_ms > 0) {
            float bpm = 60000.0f * (float)(n - 1) / (float)total_ms;
            if (bpm >= 20.0f && bpm <= 280.0f) {
                pb->bpm = bpm;
                refresh_bpm_label(pb->bpm);
                apply_transport();
            }
        }
    }
}

static void bpb_minus_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    if (pb->bpb > 1.0f) pb->bpb -= 1.0f;
    refresh_bpb_label(pb->bpb);
    apply_transport();
}

static void bpb_plus_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    if (pb->bpb < 16.0f) pb->bpb += 1.0f;
    refresh_bpb_label(pb->bpb);
    apply_transport();
}

static const int s_denoms[] = {2, 4, 8, 16};
static const int s_ndenom   = 4;

static void bpb_denom_minus_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 1; i < s_ndenom; i++) {
        if (s_denoms[i] == g_bpb_denom) {
            g_bpb_denom = s_denoms[i - 1];
            break;
        }
    }
    pedalboard_t *pb = ui_pedalboard_get();
    refresh_bpb_label(pb ? pb->bpb : 4.0f);
    refresh_bpb_denom_label();
}

static void bpb_denom_plus_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < s_ndenom - 1; i++) {
        if (s_denoms[i] == g_bpb_denom) {
            g_bpb_denom = s_denoms[i + 1];
            break;
        }
    }
    pedalboard_t *pb = ui_pedalboard_get();
    refresh_bpb_label(pb ? pb->bpb : 4.0f);
    refresh_bpb_denom_label();
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->transport_rolling = true;
    refresh_rolling_buttons(true);
    apply_transport();
}

static void stop_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->transport_rolling = false;
    refresh_rolling_buttons(false);
    apply_transport();
}

static void sync_internal_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->transport_sync = 0;
    host_transport_sync("none");
    refresh_sync_buttons(0);
    pb->modified = true;
    ui_app_update_title(pb->name, true);
}

static void sync_midi_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb->transport_sync = 1;
    host_transport_sync("midi");
    refresh_sync_buttons(1);
    pb->modified = true;
    ui_app_update_title(pb->name, true);
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    ui_conductor_close();
}

/* ─── Layout helpers ─────────────────────────────────────────────────────────── */

/* Create a labeled section container */
static lv_obj_t *make_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_pad_row(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    return box;
}

/* Create a +/− control row: [−10][−1][ value label ][+1][+10] */
static lv_obj_t *make_inc_row(lv_obj_t *parent,
                               lv_event_cb_t minus10, lv_event_cb_t minus1,
                               lv_event_cb_t plus1,  lv_event_cb_t plus10,
                               const char *init_val,
                               lv_obj_t **label_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 60);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* helper: create a small control button */
#define MAKE_BTN(label_txt, w, cb) do { \
    lv_obj_t *b = lv_btn_create(row); \
    lv_obj_set_size(b, w, 50); \
    lv_obj_set_style_bg_color(b, UI_COLOR_SURFACE, 0); \
    lv_obj_set_style_radius(b, 6, 0); \
    lv_obj_set_style_border_width(b, 1, 0); \
    lv_obj_set_style_border_color(b, UI_COLOR_TEXT_DIM, 0); \
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL); \
    lv_obj_t *l = lv_label_create(b); \
    lv_label_set_text(l, label_txt); \
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, 0); \
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0); \
    lv_obj_center(l); \
} while(0)

    if (minus10) { MAKE_BTN("-10", 70, minus10); }
    MAKE_BTN("-", 54, minus1);

    /* Value label — flex-grow to fill center */
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, init_val);
    lv_obj_set_style_text_color(val, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_32, 0);
    lv_obj_set_flex_grow(val, 1);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    if (label_out) *label_out = val;

    MAKE_BTN("+", 54, plus1);
    if (plus10) { MAKE_BTN("+10", 70, plus10); }

#undef MAKE_BTN
    return row;
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_conductor_open(void)
{
    if (g_overlay) return;   /* already open */

    pedalboard_t *pb = ui_pedalboard_get();
    float bpm = pb ? pb->bpm  : 120.0f;
    float bpb = pb ? pb->bpb  : 4.0f;
    bool  roll = pb ? pb->transport_rolling : false;
    int   sync = pb ? pb->transport_sync    : 0;

    /* ── Full-screen dim overlay ── */
    g_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(g_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
    /* Tapping the overlay backdrop closes the dialog */
    lv_obj_add_event_cb(g_overlay, close_cb, LV_EVENT_CLICKED, NULL);

    /* ── Dialog panel (centered, fixed width) ── */
    lv_obj_t *panel = lv_obj_create(g_overlay);
    lv_obj_set_size(panel, 700, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    /* Stop click propagation so backdrop close doesn't fire */
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, (lv_event_cb_t)lv_event_stop_bubbling, LV_EVENT_CLICKED, NULL);

    /* ── Title row ── */
    lv_obj_t *title_row = lv_obj_create(panel);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text_fmt(title, LV_SYMBOL_AUDIO "  %s", TR(TR_CONDUCTOR_TITLE));
    lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *btn_x = lv_btn_create(title_row);
    lv_obj_set_size(btn_x, 40, 40);
    lv_obj_align(btn_x, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_x, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(btn_x, 20, 0);
    lv_obj_add_event_cb(btn_x, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_x = lv_label_create(btn_x);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(lbl_x, UI_COLOR_TEXT, 0);
    lv_obj_center(lbl_x);

    /* ── BPM section ── */
    {
        lv_obj_t *sec = make_section(panel, TR(TR_CONDUCTOR_TEMPO));

        char bpm_str[16];
        snprintf(bpm_str, sizeof(bpm_str), "%.0f", bpm);
        make_inc_row(sec,
                     bpm_minus10_cb, bpm_minus_cb,
                     bpm_plus_cb,    bpm_plus10_cb,
                     bpm_str, &g_bpm_label);

        /* Tap tempo button */
        lv_obj_t *tap = lv_btn_create(sec);
        lv_obj_set_size(tap, LV_PCT(100), 48);
        lv_obj_set_style_bg_color(tap, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_radius(tap, 8, 0);
        lv_obj_add_event_cb(tap, tap_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *tap_lbl = lv_label_create(tap);
        lv_label_set_text(tap_lbl, TR(TR_CONDUCTOR_TAP));
        lv_obj_set_style_text_color(tap_lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(tap_lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(tap_lbl);
    }

    /* ── BPB section ── */
    {
        lv_obj_t *sec = make_section(panel, TR(TR_CONDUCTOR_TIME_SIG));

        char bpb_str[16];
        snprintf(bpb_str, sizeof(bpb_str), "%.0f / %d", bpb, g_bpb_denom);
        make_inc_row(sec,
                     NULL, bpb_minus_cb,
                     bpb_plus_cb, NULL,
                     bpb_str, &g_bpb_label);

        char denom_str[8];
        snprintf(denom_str, sizeof(denom_str), "%d", g_bpb_denom);
        make_inc_row(sec,
                     NULL, bpb_denom_minus_cb,
                     bpb_denom_plus_cb, NULL,
                     denom_str, &g_bpb_denom_label);

        lv_obj_t *denom_hint = lv_label_create(sec);
        lv_label_set_text(denom_hint, TR(TR_CONDUCTOR_DENOM));
        lv_obj_set_style_text_color(denom_hint, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(denom_hint, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(denom_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(denom_hint, LV_PCT(100));
    }

    /* ── Clock source section ── */
    {
        lv_obj_t *sec = make_section(panel, TR(TR_CONDUCTOR_CLOCK));

        lv_obj_t *row = lv_obj_create(sec);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 54);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

#define SYNC_BTN(obj, lbl_txt, cb) do { \
    obj = lv_btn_create(row); \
    lv_obj_set_size(obj, 200, 50); \
    lv_obj_set_style_radius(obj, 8, 0); \
    lv_obj_set_style_border_width(obj, 1, 0); \
    lv_obj_set_style_border_color(obj, UI_COLOR_TEXT_DIM, 0); \
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL); \
    lv_obj_t *_l = lv_label_create(obj); \
    lv_label_set_text(_l, lbl_txt); \
    lv_obj_set_style_text_color(_l, UI_COLOR_TEXT, 0); \
    lv_obj_set_style_text_font(_l, &lv_font_montserrat_16, 0); \
    lv_obj_center(_l); \
} while(0)

        SYNC_BTN(g_btn_int,  TR(TR_CONDUCTOR_INTERNAL),  sync_internal_cb);
        SYNC_BTN(g_btn_midi, TR(TR_CONDUCTOR_MIDI_SLAVE), sync_midi_cb);
#undef SYNC_BTN

        refresh_sync_buttons(sync);
    }

    /* ── Play / Stop section ── */
    {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 64);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

#define TRANSPORT_BTN(obj, lbl_txt, cb) do { \
    obj = lv_btn_create(row); \
    lv_obj_set_size(obj, 260, 58); \
    lv_obj_set_style_radius(obj, 10, 0); \
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL); \
    lv_obj_t *_l = lv_label_create(obj); \
    lv_label_set_text(_l, lbl_txt); \
    lv_obj_set_style_text_color(_l, UI_COLOR_TEXT, 0); \
    lv_obj_set_style_text_font(_l, &lv_font_montserrat_20, 0); \
    lv_obj_center(_l); \
} while(0)

        /* Prepend LVGL symbols (defined in lvgl.h, available here) */
        char play_lbl[64], stop_lbl[64];
        snprintf(play_lbl, sizeof(play_lbl), LV_SYMBOL_PLAY " %s", TR(TR_CONDUCTOR_PLAY));
        snprintf(stop_lbl, sizeof(stop_lbl), LV_SYMBOL_STOP " %s", TR(TR_CONDUCTOR_STOP));
        TRANSPORT_BTN(g_btn_play, play_lbl, play_cb);
        TRANSPORT_BTN(g_btn_stop, stop_lbl, stop_cb);
#undef TRANSPORT_BTN

        refresh_rolling_buttons(roll);
    }

    g_tap_count = 0;
}

void ui_conductor_close(void)
{
    if (!g_overlay) return;
    lv_obj_delete_async(g_overlay);
    g_overlay         = NULL;
    g_bpm_label       = NULL;
    g_bpb_label       = NULL;
    g_bpb_denom_label = NULL;
    g_btn_play        = NULL;
    g_btn_stop        = NULL;
    g_btn_int         = NULL;
    g_btn_midi        = NULL;
}

void ui_conductor_refresh(void)
{
    if (!g_overlay) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    refresh_bpm_label(pb->bpm);
    refresh_bpb_label(pb->bpb);
    refresh_rolling_buttons(pb->transport_rolling);
    refresh_sync_buttons(pb->transport_sync);
}
