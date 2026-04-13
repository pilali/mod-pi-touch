#include "ui_pre_fx.h"
#include "ui_app.h"
#include "../pre_fx.h"
#include "../settings.h"
#include "../i18n.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ─── Note names ──────────────────────────────────────────────────────────────── */
static const char * const s_note_names[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

/* ─── Overlay geometry ────────────────────────────────────────────────────────── */
#define PANEL_W 720
#define PANEL_H 500

/* ─── Internal state ──────────────────────────────────────────────────────────── */
static lv_obj_t   *g_overlay      = NULL;
static lv_obj_t   *g_panel        = NULL;
static lv_timer_t *g_refresh_timer = NULL;

/* Tab buttons */
static lv_obj_t *g_tab_tuner  = NULL;
static lv_obj_t *g_tab_gate   = NULL;
static lv_obj_t *g_body_tuner = NULL;
static lv_obj_t *g_body_gate  = NULL;

/* Tuner widgets */
static lv_obj_t *g_note_label  = NULL;  /* "A  4" */
static lv_obj_t *g_freq_label  = NULL;  /* "440.0 Hz" */
static lv_obj_t *g_cents_bar   = NULL;  /* -50..+50 */
static lv_obj_t *g_ref_label   = NULL;  /* "440 Hz" */

/* Gate widgets */
static lv_obj_t *g_gate_toggle     = NULL;
static lv_obj_t *g_gate_thr_slider = NULL;
static lv_obj_t *g_gate_thr_label  = NULL;
static lv_obj_t *g_gate_dec_slider = NULL;
static lv_obj_t *g_gate_dec_label  = NULL;
static lv_obj_t *g_mode_btns[4]    = {NULL,NULL,NULL,NULL};

/* Pedalboard mute toggle */
static lv_obj_t *g_mute_toggle = NULL;
static bool      g_pb_muted    = false;

/* ─── Mute helpers ────────────────────────────────────────────────────────────── */
/* Forward-declared by ui_pedalboard.h — include at top causes a cycle, so we
 * declare it directly here. */
void ui_pedalboard_chain_bypass(bool bypass_all);

/* ─── Tab switching ───────────────────────────────────────────────────────────── */
static void show_tab(bool tuner)
{
    if (!g_body_tuner || !g_body_gate) return;
    if (tuner) {
        lv_obj_clear_flag(g_body_tuner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (g_body_gate,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(g_tab_tuner, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_color(g_tab_gate,  UI_COLOR_SURFACE, 0);
        pre_fx_tuner_start_monitoring();
    } else {
        lv_obj_add_flag  (g_body_tuner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_body_gate,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(g_tab_tuner, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(g_tab_gate,  UI_COLOR_PRIMARY, 0);
        pre_fx_tuner_stop_monitoring();
    }
}

/* ─── Tuner refresh timer ─────────────────────────────────────────────────────── */
static void tuner_refresh_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_note_label) return;

    pre_fx_tuner_t data = pre_fx_get_tuner();

    /* Note + octave */
    if (data.freq_hz < 10.0f) {
        lv_label_set_text(g_note_label, "--");
        lv_label_set_text(g_freq_label, "-- Hz");
        lv_bar_set_value(g_cents_bar, 0, LV_ANIM_OFF);
    } else {
        char notebuf[16];
        snprintf(notebuf, sizeof(notebuf), "%s %d",
                 s_note_names[data.note & 11], data.octave);
        lv_label_set_text(g_note_label, notebuf);

        char freqbuf[32];
        snprintf(freqbuf, sizeof(freqbuf), "%.1f Hz", (double)data.freq_hz);
        lv_label_set_text(g_freq_label, freqbuf);

        int cents = (int)data.cent;
        if (cents < -50) cents = -50;
        if (cents >  50) cents =  50;
        lv_bar_set_value(g_cents_bar, cents, LV_ANIM_OFF);
    }
}

/* ─── Gate callbacks ──────────────────────────────────────────────────────────── */
static void gate_toggle_cb(lv_event_t *e)
{
    (void)e;
    mpt_settings_t *s = settings_get();
    s->gate_enabled = lv_obj_has_state(g_gate_toggle, LV_STATE_CHECKED);
    pre_fx_apply_gate();
    settings_save_prefs(s);
}

static void gate_thr_cb(lv_event_t *e)
{
    (void)e;
    mpt_settings_t *s = settings_get();
    int v = lv_slider_get_value(g_gate_thr_slider);
    s->gate_threshold = (float)v;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d dB", v);
    lv_label_set_text(g_gate_thr_label, buf);
    pre_fx_apply_gate();
}
static void gate_thr_released_cb(lv_event_t *e)
{
    (void)e;
    settings_save_prefs(settings_get());
}

static void gate_dec_cb(lv_event_t *e)
{
    (void)e;
    mpt_settings_t *s = settings_get();
    int v = lv_slider_get_value(g_gate_dec_slider);
    s->gate_decay = (float)v;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d ms", v);
    lv_label_set_text(g_gate_dec_label, buf);
    pre_fx_apply_gate();
}
static void gate_dec_released_cb(lv_event_t *e)
{
    (void)e;
    settings_save_prefs(settings_get());
}

static void mode_btn_cb(lv_event_t *e)
{
    int mode = (int)(intptr_t)lv_event_get_user_data(e);
    mpt_settings_t *s = settings_get();
    s->gate_mode = mode;
    pre_fx_apply_gate();
    settings_save_prefs(s);
    /* Update button highlights */
    for (int i = 0; i < 4; i++) {
        if (!g_mode_btns[i]) continue;
        lv_obj_set_style_bg_color(g_mode_btns[i],
            (i == mode) ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0);
    }
}

/* ─── Mute toggle callback ────────────────────────────────────────────────────── */
static void mute_toggle_cb(lv_event_t *e)
{
    (void)e;
    g_pb_muted = lv_obj_has_state(g_mute_toggle, LV_STATE_CHECKED);
    ui_pedalboard_chain_bypass(g_pb_muted);
}

/* ─── Reference frequency callbacks ──────────────────────────────────────────── */
static void ref_minus_cb(lv_event_t *e)
{
    (void)e;
    mpt_settings_t *s = settings_get();
    if (s->tuner_ref_freq > 220.0f) s->tuner_ref_freq -= 1.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f Hz", (double)s->tuner_ref_freq);
    lv_label_set_text(g_ref_label, buf);
    pre_fx_apply_tuner_ref();
    settings_save_prefs(s);
}

static void ref_plus_cb(lv_event_t *e)
{
    (void)e;
    mpt_settings_t *s = settings_get();
    if (s->tuner_ref_freq < 880.0f) s->tuner_ref_freq += 1.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f Hz", (double)s->tuner_ref_freq);
    lv_label_set_text(g_ref_label, buf);
    pre_fx_apply_tuner_ref();
    settings_save_prefs(s);
}

/* ─── Close button ────────────────────────────────────────────────────────────── */
static void close_cb(lv_event_t *e)
{
    (void)e;
    ui_pre_fx_close();
}

static void overlay_click_cb(lv_event_t *e)
{
    /* Close when the transparent overlay background is tapped (not the panel) */
    lv_obj_t *target = lv_event_get_target(e);
    if (target == g_overlay) ui_pre_fx_close();
}

/* ─── Tab button callbacks ────────────────────────────────────────────────────── */
static void tab_tuner_cb(lv_event_t *e)  { (void)e; show_tab(true); }
static void tab_gate_cb(lv_event_t *e)   { (void)e; show_tab(false); }

/* ─── Body builders ───────────────────────────────────────────────────────────── */

static lv_obj_t *make_slider_row(lv_obj_t *parent,
                                 const char *title,
                                 int min, int max, int initial,
                                 lv_event_cb_t value_cb,
                                 lv_event_cb_t released_cb,
                                 lv_obj_t **slider_out,
                                 lv_obj_t **label_out,
                                 const char *unit)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(row);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

    lv_obj_t *slider = lv_slider_create(row);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);
    lv_obj_set_size(slider, LV_PCT(75), 28);
    lv_obj_set_pos(slider, 0, 28);
    lv_obj_set_style_bg_color(slider, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    if (value_cb)   lv_obj_add_event_cb(slider, value_cb,    LV_EVENT_VALUE_CHANGED, NULL);
    if (released_cb) lv_obj_add_event_cb(slider, released_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *val_lbl = lv_label_create(row);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d %s", initial, unit);
    lv_label_set_text(val_lbl, buf);
    lv_obj_set_style_text_color(val_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(val_lbl, LV_PCT(78), 28 + 4);

    if (slider_out) *slider_out = slider;
    if (label_out)  *label_out  = val_lbl;
    return row;
}

static void build_tuner_body(lv_obj_t *parent)
{
    mpt_settings_t *s = settings_get();

    /* ── Note display ── */
    g_note_label = lv_label_create(parent);
    lv_label_set_text(g_note_label, "--");
    lv_obj_set_style_text_font(g_note_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(g_note_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(g_note_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_note_label, LV_PCT(100));
    lv_obj_align(g_note_label, LV_ALIGN_TOP_MID, 0, 10);

    /* ── Frequency display ── */
    g_freq_label = lv_label_create(parent);
    lv_label_set_text(g_freq_label, "-- Hz");
    lv_obj_set_style_text_font(g_freq_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_freq_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(g_freq_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_freq_label, LV_PCT(100));
    lv_obj_align(g_freq_label, LV_ALIGN_TOP_MID, 0, 74);

    /* ── Cents bar ── */
    g_cents_bar = lv_bar_create(parent);
    lv_bar_set_mode(g_cents_bar, LV_BAR_MODE_SYMMETRICAL);
    lv_bar_set_range(g_cents_bar, -50, 50);
    lv_bar_set_value(g_cents_bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(g_cents_bar, 540, 24);
    lv_obj_align(g_cents_bar, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_style_bg_color(g_cents_bar, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_cents_bar, UI_COLOR_ACCENT,  LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_cents_bar, 4, 0);

    /* Cents scale labels */
    lv_obj_t *lbl_neg = lv_label_create(parent);
    lv_label_set_text(lbl_neg, "-50¢");
    lv_obj_set_style_text_color(lbl_neg, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_neg, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_neg, LV_ALIGN_TOP_MID, -260, 143);

    lv_obj_t *lbl_zero = lv_label_create(parent);
    lv_label_set_text(lbl_zero, "0");
    lv_obj_set_style_text_color(lbl_zero, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_zero, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_zero, LV_ALIGN_TOP_MID, 0, 143);

    lv_obj_t *lbl_pos = lv_label_create(parent);
    lv_label_set_text(lbl_pos, "+50¢");
    lv_obj_set_style_text_color(lbl_pos, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_pos, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_pos, LV_ALIGN_TOP_MID, 260, 143);

    /* ── Reference frequency row ── */
    lv_obj_t *ref_row = lv_obj_create(parent);
    lv_obj_set_size(ref_row, LV_PCT(100), 50);
    lv_obj_align(ref_row, LV_ALIGN_TOP_MID, 0, 175);
    lv_obj_set_style_bg_opa(ref_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ref_row, 0, 0);
    lv_obj_set_style_pad_all(ref_row, 0, 0);
    lv_obj_clear_flag(ref_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ref_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ref_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ref_row, 12, 0);

    lv_obj_t *ref_title = lv_label_create(ref_row);
    lv_label_set_text(ref_title, TR(TR_PREFX_REF_FREQ));
    lv_obj_set_style_text_color(ref_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ref_title, &lv_font_montserrat_16, 0);

    lv_obj_t *btn_minus = lv_btn_create(ref_row);
    lv_obj_set_size(btn_minus, 50, 40);
    lv_obj_set_style_bg_color(btn_minus, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(btn_minus, 6, 0);
    lv_obj_t *lbl_m = lv_label_create(btn_minus);
    lv_label_set_text(lbl_m, LV_SYMBOL_MINUS);
    lv_obj_center(lbl_m);
    lv_obj_add_event_cb(btn_minus, ref_minus_cb, LV_EVENT_CLICKED, NULL);

    char refbuf[16];
    snprintf(refbuf, sizeof(refbuf), "%.0f Hz", (double)s->tuner_ref_freq);
    g_ref_label = lv_label_create(ref_row);
    lv_label_set_text(g_ref_label, refbuf);
    lv_obj_set_style_text_color(g_ref_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(g_ref_label, &lv_font_montserrat_20, 0);
    lv_obj_set_width(g_ref_label, 90);
    lv_obj_set_style_text_align(g_ref_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn_plus = lv_btn_create(ref_row);
    lv_obj_set_size(btn_plus, 50, 40);
    lv_obj_set_style_bg_color(btn_plus, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(btn_plus, 6, 0);
    lv_obj_t *lbl_p = lv_label_create(btn_plus);
    lv_label_set_text(lbl_p, LV_SYMBOL_PLUS);
    lv_obj_center(lbl_p);
    lv_obj_add_event_cb(btn_plus, ref_plus_cb, LV_EVENT_CLICKED, NULL);

    /* ── Mute toggle ── */
    lv_obj_t *mute_row = lv_obj_create(parent);
    lv_obj_set_size(mute_row, LV_PCT(100), 50);
    lv_obj_align(mute_row, LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_set_style_bg_opa(mute_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mute_row, 0, 0);
    lv_obj_set_style_pad_all(mute_row, 0, 0);
    lv_obj_clear_flag(mute_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(mute_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mute_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mute_row, 12, 0);

    lv_obj_t *mute_lbl = lv_label_create(mute_row);
    lv_label_set_text(mute_lbl, TR(TR_PREFX_MUTE_PB));
    lv_obj_set_style_text_color(mute_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(mute_lbl, &lv_font_montserrat_16, 0);

    g_mute_toggle = lv_switch_create(mute_row);
    lv_obj_set_size(g_mute_toggle, 60, 32);
    if (g_pb_muted) lv_obj_add_state(g_mute_toggle, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_mute_toggle, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_mute_toggle, UI_COLOR_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_mute_toggle, mute_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_gate_body(lv_obj_t *parent)
{
    mpt_settings_t *s = settings_get();

    /* ── Enable row ── */
    lv_obj_t *en_row = lv_obj_create(parent);
    lv_obj_set_size(en_row, LV_PCT(100), 50);
    lv_obj_align(en_row, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_opa(en_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(en_row, 0, 0);
    lv_obj_set_style_pad_all(en_row, 0, 0);
    lv_obj_clear_flag(en_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(en_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(en_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(en_row, 16, 0);

    lv_obj_t *en_lbl = lv_label_create(en_row);
    lv_label_set_text(en_lbl, TR(TR_PREFX_GATE_ENABLED));
    lv_obj_set_style_text_color(en_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(en_lbl, &lv_font_montserrat_16, 0);

    g_gate_toggle = lv_switch_create(en_row);
    lv_obj_set_size(g_gate_toggle, 60, 32);
    if (s->gate_enabled) lv_obj_add_state(g_gate_toggle, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_gate_toggle, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_gate_toggle, UI_COLOR_ACTIVE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_gate_toggle, gate_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Threshold slider ── */
    lv_obj_t *thr_cont = lv_obj_create(parent);
    lv_obj_set_size(thr_cont, LV_PCT(100), 70);
    lv_obj_align(thr_cont, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_opa(thr_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(thr_cont, 0, 0);
    lv_obj_set_style_pad_all(thr_cont, 0, 0);
    lv_obj_clear_flag(thr_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *thr_title = lv_label_create(thr_cont);
    lv_label_set_text(thr_title, TR(TR_PREFX_THRESHOLD));
    lv_obj_set_style_text_color(thr_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(thr_title, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(thr_title, 10, 0);

    g_gate_thr_slider = lv_slider_create(thr_cont);
    lv_slider_set_range(g_gate_thr_slider, -70, -10);
    lv_slider_set_value(g_gate_thr_slider, (int)s->gate_threshold, LV_ANIM_OFF);
    lv_obj_set_size(g_gate_thr_slider, 500, 28);
    lv_obj_set_pos(g_gate_thr_slider, 10, 32);
    lv_obj_set_style_bg_color(g_gate_thr_slider, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_gate_thr_slider, UI_COLOR_ACCENT,  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_gate_thr_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(g_gate_thr_slider, gate_thr_cb,          LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_gate_thr_slider, gate_thr_released_cb, LV_EVENT_RELEASED,      NULL);

    char thrbuf[16];
    snprintf(thrbuf, sizeof(thrbuf), "%d dB", (int)s->gate_threshold);
    g_gate_thr_label = lv_label_create(thr_cont);
    lv_label_set_text(g_gate_thr_label, thrbuf);
    lv_obj_set_style_text_color(g_gate_thr_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(g_gate_thr_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_gate_thr_label, 520, 32 + 4);

    /* ── Decay slider ── */
    lv_obj_t *dec_cont = lv_obj_create(parent);
    lv_obj_set_size(dec_cont, LV_PCT(100), 70);
    lv_obj_align(dec_cont, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_bg_opa(dec_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dec_cont, 0, 0);
    lv_obj_set_style_pad_all(dec_cont, 0, 0);
    lv_obj_clear_flag(dec_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dec_title = lv_label_create(dec_cont);
    lv_label_set_text(dec_title, TR(TR_PREFX_DECAY));
    lv_obj_set_style_text_color(dec_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(dec_title, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(dec_title, 10, 0);

    g_gate_dec_slider = lv_slider_create(dec_cont);
    lv_slider_set_range(g_gate_dec_slider, 1, 500);
    lv_slider_set_value(g_gate_dec_slider, (int)s->gate_decay, LV_ANIM_OFF);
    lv_obj_set_size(g_gate_dec_slider, 500, 28);
    lv_obj_set_pos(g_gate_dec_slider, 10, 32);
    lv_obj_set_style_bg_color(g_gate_dec_slider, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_gate_dec_slider, UI_COLOR_ACCENT,  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_gate_dec_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(g_gate_dec_slider, gate_dec_cb,          LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_gate_dec_slider, gate_dec_released_cb, LV_EVENT_RELEASED,      NULL);

    char decbuf[16];
    snprintf(decbuf, sizeof(decbuf), "%d ms", (int)s->gate_decay);
    g_gate_dec_label = lv_label_create(dec_cont);
    lv_label_set_text(g_gate_dec_label, decbuf);
    lv_obj_set_style_text_color(g_gate_dec_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(g_gate_dec_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_gate_dec_label, 520, 32 + 4);

    /* ── Mode buttons ── */
    lv_obj_t *mode_title = lv_label_create(parent);
    lv_label_set_text(mode_title, TR(TR_PREFX_MODE));
    lv_obj_set_style_text_color(mode_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(mode_title, &lv_font_montserrat_16, 0);
    lv_obj_align(mode_title, LV_ALIGN_TOP_LEFT, 10, 235);

    lv_obj_t *mode_row = lv_obj_create(parent);
    lv_obj_set_size(mode_row, LV_PCT(100), 52);
    lv_obj_align(mode_row, LV_ALIGN_TOP_MID, 0, 260);
    lv_obj_set_style_bg_opa(mode_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mode_row, 0, 0);
    lv_obj_set_style_pad_all(mode_row, 0, 0);
    lv_obj_clear_flag(mode_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(mode_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mode_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mode_row, 10, 0);

    const char *mode_labels[4] = {
        TR(TR_PREFX_MODE_OFF),
        TR(TR_PREFX_MODE_IN1),
        TR(TR_PREFX_MODE_IN2),
        TR(TR_PREFX_MODE_STEREO)
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *mb = lv_btn_create(mode_row);
        lv_obj_set_size(mb, 140, 44);
        lv_obj_set_style_bg_color(mb,
            (i == s->gate_mode) ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_radius(mb, 6, 0);
        lv_obj_t *ml = lv_label_create(mb);
        lv_label_set_text(ml, mode_labels[i]);
        lv_obj_set_style_text_color(ml, UI_COLOR_TEXT, 0);
        lv_obj_center(ml);
        lv_obj_add_event_cb(mb, mode_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_mode_btns[i] = mb;
    }
}

/* ─── Public API ──────────────────────────────────────────────────────────────── */

void ui_pre_fx_open(void)
{
    if (g_overlay) return; /* already open */

    /* ── Transparent overlay ── */
    g_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(g_overlay, 0, 0);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_overlay, overlay_click_cb, LV_EVENT_CLICKED, NULL);

    /* ── Panel ── */
    g_panel = lv_obj_create(g_overlay);
    lv_obj_set_size(g_panel, PANEL_W, PANEL_H);
    lv_obj_align(g_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(g_panel, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_width(g_panel, 1, 0);
    lv_obj_set_style_radius(g_panel, 12, 0);
    lv_obj_set_style_pad_all(g_panel, 0, 0);
    lv_obj_clear_flag(g_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header bar ── */
    lv_obj_t *header = lv_obj_create(g_panel);
    lv_obj_set_size(header, PANEL_W, 56);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Tab buttons */
    g_tab_tuner = lv_btn_create(header);
    lv_obj_set_size(g_tab_tuner, 160, 46);
    lv_obj_set_pos(g_tab_tuner, 8, 5);
    lv_obj_set_style_bg_color(g_tab_tuner, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(g_tab_tuner, 6, 0);
    lv_obj_t *tl = lv_label_create(g_tab_tuner);
    lv_label_set_text(tl, TR(TR_PREFX_TUNER));
    lv_obj_set_style_text_color(tl, UI_COLOR_TEXT, 0);
    lv_obj_center(tl);
    lv_obj_add_event_cb(g_tab_tuner, tab_tuner_cb, LV_EVENT_CLICKED, NULL);

    g_tab_gate = lv_btn_create(header);
    lv_obj_set_size(g_tab_gate, 160, 46);
    lv_obj_set_pos(g_tab_gate, 176, 5);
    lv_obj_set_style_bg_color(g_tab_gate, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(g_tab_gate, 6, 0);
    lv_obj_t *gl = lv_label_create(g_tab_gate);
    lv_label_set_text(gl, TR(TR_PREFX_NOISEGATE));
    lv_obj_set_style_text_color(gl, UI_COLOR_TEXT, 0);
    lv_obj_center(gl);
    lv_obj_add_event_cb(g_tab_gate, tab_gate_cb, LV_EVENT_CLICKED, NULL);

    /* Close button */
    lv_obj_t *btn_close = lv_btn_create(header);
    lv_obj_set_size(btn_close, 46, 46);
    lv_obj_set_pos(btn_close, PANEL_W - 54, 5);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0xC0392B), 0);
    lv_obj_set_style_radius(btn_close, 6, 0);
    lv_obj_t *cl = lv_label_create(btn_close);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(cl, lv_color_white(), 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(btn_close, close_cb, LV_EVENT_CLICKED, NULL);

    /* ── Body containers ── */
    g_body_tuner = lv_obj_create(g_panel);
    lv_obj_set_size(g_body_tuner, PANEL_W - 20, PANEL_H - 70);
    lv_obj_set_pos(g_body_tuner, 10, 60);
    lv_obj_set_style_bg_opa(g_body_tuner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_body_tuner, 0, 0);
    lv_obj_set_style_pad_all(g_body_tuner, 0, 0);
    lv_obj_clear_flag(g_body_tuner, LV_OBJ_FLAG_SCROLLABLE);

    g_body_gate = lv_obj_create(g_panel);
    lv_obj_set_size(g_body_gate, PANEL_W - 20, PANEL_H - 70);
    lv_obj_set_pos(g_body_gate, 10, 60);
    lv_obj_set_style_bg_opa(g_body_gate, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_body_gate, 0, 0);
    lv_obj_set_style_pad_all(g_body_gate, 0, 0);
    lv_obj_clear_flag(g_body_gate, LV_OBJ_FLAG_SCROLLABLE);

    build_tuner_body(g_body_tuner);
    build_gate_body(g_body_gate);

    /* Start on tuner tab */
    show_tab(true);

    /* ── Refresh timer (100 ms) ── */
    g_refresh_timer = lv_timer_create(tuner_refresh_cb, 100, NULL);
}

void ui_pre_fx_close(void)
{
    if (!g_overlay) return;

    /* Stop monitoring */
    pre_fx_tuner_stop_monitoring();

    /* Restore mute state */
    if (g_pb_muted) {
        g_pb_muted = false;
        ui_pedalboard_chain_bypass(false);
    }

    /* Delete timer */
    if (g_refresh_timer) {
        lv_timer_del(g_refresh_timer);
        g_refresh_timer = NULL;
    }

    /* Null all widget pointers before deleting the tree */
    g_note_label = g_freq_label = g_cents_bar = g_ref_label = NULL;
    g_gate_toggle = g_gate_thr_slider = g_gate_thr_label = NULL;
    g_gate_dec_slider = g_gate_dec_label = NULL;
    g_mute_toggle = NULL;
    g_tab_tuner = g_tab_gate = g_body_tuner = g_body_gate = NULL;
    for (int i = 0; i < 4; i++) g_mode_btns[i] = NULL;

    lv_obj_delete_async(g_overlay);
    g_overlay = NULL;
    g_panel   = NULL;
}

int ui_pre_fx_is_open(void)
{
    return g_overlay != NULL;
}
