#include "ui_splash.h"
#include "ui_app.h"
#include "../i18n.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── Colors (match main UI palette) ────────────────────────────────────────── */
#define SPLASH_BG       lv_color_hex(0x111827)  /* deep dark blue-grey */
#define SPLASH_TITLE    lv_color_hex(0xffffff)
#define SPLASH_SUBTITLE lv_color_hex(0x8090b0)
#define SPLASH_BAR_BG   lv_color_hex(0x252d3d)
#define SPLASH_BAR_FG   lv_color_hex(0x3d7af5)  /* primary blue */
#define SPLASH_MSG      lv_color_hex(0xa0b0c8)

/* ─── State ─────────────────────────────────────────────────────────────────── */
static lv_obj_t *g_overlay  = NULL;
static lv_obj_t *g_bar_fill = NULL;   /* inner progress fill */
static lv_obj_t *g_msg_lbl  = NULL;   /* status message */
static lv_obj_t *g_pct_lbl  = NULL;   /* "70%" */
static int       g_pct      = 0;

/* ─── Helpers ─────────────────────────────────────────────────────────────────── */

static void apply_progress(int pct, const char *msg)
{
    if (!g_overlay) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_pct = pct;

    /* Resize fill bar: bar track is 600px wide */
    if (g_bar_fill) {
        lv_obj_set_width(g_bar_fill, (600 * pct) / 100);
    }
    if (g_msg_lbl && msg)
        lv_label_set_text(g_msg_lbl, msg);
    if (g_pct_lbl) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(g_pct_lbl, buf);
    }
}

/* ─── Async update (thread-safe) ─────────────────────────────────────────────── */

typedef struct { int pct; char msg[128]; } splash_upd_t;

static void splash_upd_cb(void *arg)
{
    splash_upd_t *u = arg;
    apply_progress(u->pct, u->msg);
    free(u);
}

/* ─── Fade-out animation ─────────────────────────────────────────────────────── */

static void opa_anim_cb(void *obj, int32_t val)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
}

static void fade_done_cb(lv_anim_t *a)
{
    lv_obj_t *obj = lv_anim_get_user_data(a);
    if (obj) lv_obj_del(obj);
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_splash_show(void)
{
    if (g_overlay) return;

    lv_obj_t *scr = lv_layer_top();

    /* Full-screen overlay */
    g_overlay = lv_obj_create(scr);
    lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_overlay, SPLASH_BG, 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Centered content column ── */
    lv_obj_t *col = lv_obj_create(g_overlay);
    lv_obj_set_size(col, 700, LV_SIZE_CONTENT);
    lv_obj_center(col);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 18, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(col);
    lv_label_set_text(title, "mod-pi-touch");
    lv_obj_set_style_text_color(title, SPLASH_TITLE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(col);
    lv_label_set_text(sub, "LVGL UI for mod-host");
    lv_obj_set_style_text_color(sub, SPLASH_SUBTITLE, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);

    /* Spacer */
    lv_obj_t *sp = lv_obj_create(col);
    lv_obj_set_size(sp, 1, 12);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp, 0, 0);

    /* Progress bar track */
    lv_obj_t *bar_track = lv_obj_create(col);
    lv_obj_set_size(bar_track, 600, 8);
    lv_obj_set_style_bg_color(bar_track, SPLASH_BAR_BG, 0);
    lv_obj_set_style_bg_opa(bar_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_track, 0, 0);
    lv_obj_set_style_radius(bar_track, 4, 0);
    lv_obj_set_style_pad_all(bar_track, 0, 0);
    lv_obj_clear_flag(bar_track, LV_OBJ_FLAG_SCROLLABLE);

    /* Progress fill (child of track, anchored left) */
    g_bar_fill = lv_obj_create(bar_track);
    lv_obj_set_size(g_bar_fill, 0, 8);
    lv_obj_set_style_bg_color(g_bar_fill, SPLASH_BAR_FG, 0);
    lv_obj_set_style_bg_opa(g_bar_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_bar_fill, 0, 0);
    lv_obj_set_style_radius(g_bar_fill, 4, 0);
    lv_obj_set_style_pad_all(g_bar_fill, 0, 0);
    lv_obj_set_align(g_bar_fill, LV_ALIGN_LEFT_MID);

    /* Percentage + message row */
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_set_size(row, 600, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    g_msg_lbl = lv_label_create(row);
    lv_label_set_text(g_msg_lbl, "");
    lv_obj_set_style_text_color(g_msg_lbl, SPLASH_MSG, 0);
    lv_obj_set_style_text_font(g_msg_lbl, &lv_font_montserrat_14, 0);

    g_pct_lbl = lv_label_create(row);
    lv_label_set_text(g_pct_lbl, "0%");
    lv_obj_set_style_text_color(g_pct_lbl, SPLASH_SUBTITLE, 0);
    lv_obj_set_style_text_font(g_pct_lbl, &lv_font_montserrat_14, 0);

    g_pct = 0;
}

void ui_splash_update(int pct, const char *msg)
{
    /* If called from the LVGL thread (main.c before background thread), apply
     * directly.  Otherwise marshal through lv_async_call. */
    splash_upd_t *u = malloc(sizeof(*u));
    if (!u) return;
    u->pct = pct;
    snprintf(u->msg, sizeof(u->msg), "%s", msg ? msg : "");
    lv_async_call(splash_upd_cb, u);
}

void ui_splash_hide(void)
{
    if (!g_overlay) return;

    lv_obj_t *obj = g_overlay;
    g_overlay  = NULL;
    g_bar_fill = NULL;
    g_msg_lbl  = NULL;
    g_pct_lbl  = NULL;

    /* Short fade-out */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_user_data(&a, obj);
    lv_anim_set_completed_cb(&a, fade_done_cb);
    lv_anim_start(&a);
}

static void splash_hide_cb(void *arg)
{
    (void)arg;
    ui_splash_hide();
}

void ui_splash_hide_async(void)
{
    lv_async_call(splash_hide_cb, NULL);
}

void ui_splash_show_power(int type)
{
    /* Delete any existing splash first */
    if (g_overlay) {
        lv_obj_del(g_overlay);
        g_overlay  = NULL;
        g_bar_fill = NULL;
        g_msg_lbl  = NULL;
        g_pct_lbl  = NULL;
    }

    const char *msg  = (type == 0) ? TR(TR_SPLASH_SHUTTING_DOWN)
                                   : TR(TR_SPLASH_REBOOTING);
    const char *icon = (type == 0) ? LV_SYMBOL_POWER : LV_SYMBOL_REFRESH;

    lv_obj_t *scr = lv_layer_top();

    g_overlay = lv_obj_create(scr);
    lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_overlay, SPLASH_BG, 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *col = lv_obj_create(g_overlay);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(col);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 20, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon_lbl = lv_label_create(col);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, SPLASH_SUBTITLE, 0);
    lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_32, 0);

    lv_obj_t *msg_lbl = lv_label_create(col);
    lv_label_set_text(msg_lbl, msg);
    lv_obj_set_style_text_color(msg_lbl, SPLASH_TITLE, 0);
    lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_24, 0);

    /* Force two render passes so the framebuffer is actually flushed */
    lv_timer_handler();
    lv_timer_handler();
}
