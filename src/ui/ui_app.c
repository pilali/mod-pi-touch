#include "ui_app.h"
#include "ui_pedalboard.h"
#include "ui_plugin_browser.h"
#include "ui_bank_browser.h"
#include "ui_settings.h"
#include "ui_snapshot_bar.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── Global theme colors ────────────────────────────────────────────────────── */
lv_color_t UI_COLOR_BG        = {0};
lv_color_t UI_COLOR_SURFACE   = {0};
lv_color_t UI_COLOR_PRIMARY   = {0};
lv_color_t UI_COLOR_ACCENT    = {0};
lv_color_t UI_COLOR_TEXT      = {0};
lv_color_t UI_COLOR_TEXT_DIM  = {0};
lv_color_t UI_COLOR_BYPASS    = {0};
lv_color_t UI_COLOR_ACTIVE    = {0};

/* ─── Internal state ─────────────────────────────────────────────────────────── */
static lv_obj_t *g_screen       = NULL;
static lv_obj_t *g_top_bar      = NULL;
static lv_obj_t *g_content_area = NULL;
static lv_obj_t *g_snap_bar     = NULL;
static lv_obj_t *g_title_label  = NULL;
static lv_obj_t *g_mod_dot      = NULL;

/* ─── Color theme (dark) ─────────────────────────────────────────────────────── */
static void init_colors(void)
{
    UI_COLOR_BG       = lv_color_hex(0x1A1A1A);
    UI_COLOR_SURFACE  = lv_color_hex(0x2A2A2A);
    UI_COLOR_PRIMARY  = lv_color_hex(0xFF6B00); /* MOD orange */
    UI_COLOR_ACCENT   = lv_color_hex(0x00B4D8); /* cyan */
    UI_COLOR_TEXT     = lv_color_hex(0xEEEEEE);
    UI_COLOR_TEXT_DIM = lv_color_hex(0x888888);
    UI_COLOR_BYPASS   = lv_color_hex(0x444444);
    UI_COLOR_ACTIVE   = lv_color_hex(0x2ECC71); /* green */
}

/* ─── Top bar callbacks ──────────────────────────────────────────────────────── */
static void btn_banks_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_BANK_BROWSER);
}

static void btn_add_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PLUGIN_BROWSER);
}

static void btn_save_cb(lv_event_t *e)
{
    (void)e;
    ui_pedalboard_save();
}

static void btn_settings_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_SETTINGS);
}

/* ─── Top bar creation ───────────────────────────────────────────────────────── */
static void create_top_bar(void)
{
    g_top_bar = lv_obj_create(g_screen);
    lv_obj_set_size(g_top_bar, LV_PCT(100), UI_TOP_BAR_H);
    lv_obj_align(g_top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(g_top_bar, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(g_top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_top_bar, 0, 0);
    lv_obj_set_style_radius(g_top_bar, 0, 0);
    lv_obj_set_style_pad_all(g_top_bar, 8, 0);
    lv_obj_clear_flag(g_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Banks button (left) */
    lv_obj_t *btn_banks = lv_btn_create(g_top_bar);
    lv_obj_set_size(btn_banks, 100, 44);
    lv_obj_align(btn_banks, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_banks, UI_COLOR_PRIMARY, 0);
    lv_obj_add_event_cb(btn_banks, btn_banks_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn_banks);
    lv_label_set_text(lbl, LV_SYMBOL_LIST " Pedalboards");
    lv_obj_center(lbl);

    /* Pedalboard title (center) */
    g_title_label = lv_label_create(g_top_bar);
    lv_label_set_text(g_title_label, "No pedalboard");
    lv_obj_set_style_text_color(g_title_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_18, 0);
    lv_obj_align(g_title_label, LV_ALIGN_CENTER, 0, 0);

    /* Modified indicator ("*") */
    g_mod_dot = lv_label_create(g_top_bar);
    lv_label_set_text(g_mod_dot, "*");
    lv_obj_set_style_text_color(g_mod_dot, UI_COLOR_PRIMARY, 0);
    lv_obj_align_to(g_mod_dot, g_title_label, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_add_flag(g_mod_dot, LV_OBJ_FLAG_HIDDEN);

    /* Right-side: save / add / settings */
    lv_obj_t *btn_set = lv_btn_create(g_top_bar);
    lv_obj_set_size(btn_set, 44, 44);
    lv_obj_align(btn_set, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_set, UI_COLOR_SURFACE, 0);
    lv_obj_add_event_cb(btn_set, btn_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_set = lv_label_create(btn_set);
    lv_label_set_text(lbl_set, LV_SYMBOL_SETTINGS);
    lv_obj_center(lbl_set);

    lv_obj_t *btn_add = lv_btn_create(g_top_bar);
    lv_obj_set_size(btn_add, 44, 44);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, -(44 + 6), 0);
    lv_obj_set_style_bg_color(btn_add, UI_COLOR_ACTIVE, 0);
    lv_obj_add_event_cb(btn_add, btn_add_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS);
    lv_obj_center(lbl_add);

    lv_obj_t *btn_save = lv_btn_create(g_top_bar);
    lv_obj_set_size(btn_save, 44, 44);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, -(44 + 6) * 2, 0);
    lv_obj_set_style_bg_color(btn_save, UI_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_SAVE);
    lv_obj_center(lbl_save);
}

/* ─── Modal helpers ──────────────────────────────────────────────────────────── */

static void overlay_close_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_del(overlay);
}

typedef struct {
    ui_confirm_cb_t cb;
    void           *ud;
    lv_obj_t       *overlay;
} confirm_ctx_t;

static void confirm_ok_cb(lv_event_t *e)
{
    confirm_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_del(ctx->overlay);
    if (ctx->cb) ctx->cb(ctx->ud);
    free(ctx);
}

static void confirm_cancel_cb(lv_event_t *e)
{
    confirm_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_del(ctx->overlay);
    free(ctx);
}

typedef struct {
    ui_input_cb_t cb;
    void         *ud;
    lv_obj_t     *ta;
    lv_obj_t     *overlay;
    lv_obj_t     *kbd;
} input_ctx_t;

static void input_ok_cb(lv_event_t *e)
{
    input_ctx_t *ctx = lv_event_get_user_data(e);
    const char  *text = lv_textarea_get_text(ctx->ta);
    lv_obj_del(ctx->kbd);
    lv_obj_del(ctx->overlay);
    if (ctx->cb) ctx->cb(text, ctx->ud);
    free(ctx);
}

static void input_cancel_cb(lv_event_t *e)
{
    input_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_del(ctx->kbd);
    lv_obj_del(ctx->overlay);
    free(ctx);
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_app_init(void)
{
    init_colors();

    g_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar();

    /* Content area */
    g_content_area = lv_obj_create(g_screen);
    lv_obj_set_size(g_content_area, LV_PCT(100), UI_CANVAS_H);
    lv_obj_align(g_content_area, LV_ALIGN_TOP_MID, 0, UI_TOP_BAR_H);
    lv_obj_set_style_bg_color(g_content_area, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(g_content_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_content_area, 0, 0);
    lv_obj_set_style_pad_all(g_content_area, 0, 0);
    lv_obj_set_style_radius(g_content_area, 0, 0);
    lv_obj_clear_flag(g_content_area, LV_OBJ_FLAG_SCROLLABLE);

    /* Snapshot bar */
    g_snap_bar = lv_obj_create(g_screen);
    lv_obj_set_size(g_snap_bar, LV_PCT(100), UI_SNAPSHOT_BAR_H);
    lv_obj_align(g_snap_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g_snap_bar, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(g_snap_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_snap_bar, 0, 0);
    lv_obj_set_style_radius(g_snap_bar, 0, 0);
    lv_obj_clear_flag(g_snap_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Initialize sub-views */
    ui_pedalboard_init(g_content_area);
    ui_snapshot_bar_init(g_snap_bar);

    lv_screen_load(g_screen);
}

void ui_app_show_screen(ui_screen_t screen)
{
    lv_obj_clean(g_content_area);

    switch (screen) {
        case UI_SCREEN_PEDALBOARD:
            ui_pedalboard_init(g_content_area);
            break;
        case UI_SCREEN_PLUGIN_BROWSER:
            ui_plugin_browser_show(g_content_area);
            break;
        case UI_SCREEN_BANK_BROWSER:
            ui_bank_browser_show(g_content_area);
            break;
        case UI_SCREEN_SETTINGS:
            ui_settings_show(g_content_area);
            break;
    }
}

void ui_app_update_title(const char *name, bool modified)
{
    if (g_title_label) lv_label_set_text(g_title_label, name);
    if (g_mod_dot) {
        if (modified) lv_obj_clear_flag(g_mod_dot, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(g_mod_dot,   LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_app_show_message(const char *title, const char *body, int autodismiss_ms)
{
    (void)autodismiss_ms;
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_msgbox_add_title(box, title);
    lv_msgbox_add_text(box, body);
    lv_msgbox_add_close_button(box);
    lv_obj_center(box);
}

void ui_app_show_input(const char *title, const char *placeholder,
                       ui_input_cb_t cb, void *userdata)
{
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_center(overlay);

    lv_obj_t *box = lv_obj_create(overlay);
    lv_obj_set_size(box, 600, 200);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(box, UI_COLOR_SURFACE, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_pad_row(box, 8, 0); lv_obj_set_style_pad_column(box, 8, 0);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    lv_obj_t *ta = lv_textarea_create(box);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_one_line(ta, true);

    lv_obj_t *row = lv_obj_create(box);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_row(row, 8, 0); lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *kbd = lv_keyboard_create(overlay);
    lv_keyboard_set_textarea(kbd, ta);
    lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, 0);

    input_ctx_t *ctx = malloc(sizeof(*ctx));
    ctx->cb = cb; ctx->ud = userdata; ctx->ta = ta;
    ctx->overlay = overlay; ctx->kbd = kbd;

    lv_obj_t *btn_cancel = lv_btn_create(row);
    lv_obj_set_size(btn_cancel, 100, 36);
    lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, input_cancel_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *btn_ok = lv_btn_create(row);
    lv_obj_set_size(btn_ok, 100, 36);
    lv_obj_set_style_bg_color(btn_ok, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "OK");
    lv_obj_center(lbl_ok);
    lv_obj_add_event_cb(btn_ok, input_ok_cb, LV_EVENT_CLICKED, ctx);
}

void ui_app_show_confirm(const char *title, const char *message,
                         ui_confirm_cb_t ok_cb, void *userdata)
{
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_center(overlay);

    lv_obj_t *box = lv_obj_create(overlay);
    lv_obj_set_size(box, 500, 200);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, UI_COLOR_SURFACE, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_style_pad_row(box, 12, 0); lv_obj_set_style_pad_column(box, 12, 0);

    lv_obj_t *lbl_title = lv_label_create(box);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);

    lv_obj_t *lbl_msg = lv_label_create(box);
    lv_label_set_text(lbl_msg, message);
    lv_obj_set_style_text_color(lbl_msg, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *row = lv_obj_create(box);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_row(row, 8, 0); lv_obj_set_style_pad_column(row, 8, 0);

    confirm_ctx_t *ctx = malloc(sizeof(*ctx));
    ctx->cb = ok_cb; ctx->ud = userdata; ctx->overlay = overlay;

    lv_obj_t *btn_cancel = lv_btn_create(row);
    lv_obj_set_size(btn_cancel, 100, 40);
    lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, confirm_cancel_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *btn_ok = lv_btn_create(row);
    lv_obj_set_size(btn_ok, 100, 40);
    lv_obj_set_style_bg_color(btn_ok, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "OK");
    lv_obj_center(lbl_ok);
    lv_obj_add_event_cb(btn_ok, confirm_ok_cb, LV_EVENT_CLICKED, ctx);
}

lv_obj_t *ui_app_content_area(void) { return g_content_area; }
