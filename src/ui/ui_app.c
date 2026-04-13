#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../pedalboard.h"
#include "../settings.h"
#include "../i18n.h"
#include "ui_plugin_browser.h"
#include "ui_bank_browser.h"
#include "ui_settings.h"
#include "ui_snapshot_bar.h"
#include "ui_conductor.h"
#include "ui_pre_fx.h"

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
static lv_obj_t   *g_screen       = NULL;
static lv_obj_t   *g_top_bar      = NULL;
static lv_obj_t   *g_content_area = NULL;
static lv_obj_t   *g_snap_bar     = NULL;
static lv_obj_t   *g_title_label  = NULL;
static lv_obj_t   *g_mod_dot      = NULL;
static lv_obj_t   *g_banks_label  = NULL;  /* text sub-label of Banks button */
static lv_obj_t   *g_btn_add_float = NULL; /* floating + button on canvas */
static ui_screen_t g_current_screen = UI_SCREEN_PEDALBOARD;
/* Toast state — declared here so ui_app_show_screen can cancel it before
 * lv_obj_clean(lv_layer_top()) destroys g_toast without nulling the pointer. */
static lv_obj_t   *g_toast       = NULL;
static lv_timer_t *g_toast_timer = NULL;

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

static void btn_pre_fx_cb(lv_event_t *e)
{
    (void)e;
    if (ui_pre_fx_is_open()) ui_pre_fx_close();
    else                     ui_pre_fx_open();
}

static void btn_conductor_cb(lv_event_t *e)
{
    (void)e;
    ui_conductor_open();
}

static void btn_add_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PLUGIN_BROWSER);
}

/* ─── Save menu ──────────────────────────────────────────────────────────────── */

static lv_obj_t *g_save_menu = NULL;

/* Sync close — safe only when called from outside the save menu's event tree
 * (e.g. nav bar buttons, ui_app_show_screen). */
static void save_menu_close(void)
{
    if (g_save_menu) { lv_obj_del(g_save_menu); g_save_menu = NULL; }
}

/* Async close — required when called from a callback whose sender is a child
 * of g_save_menu (or g_save_menu itself), to avoid deleting the parent while
 * LVGL is still dispatching the event. */
static void save_menu_close_async(void)
{
    if (g_save_menu) {
        lv_obj_add_flag(g_save_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(g_save_menu);
        g_save_menu = NULL;
    }
}

static void save_menu_overlay_cb(lv_event_t *e) { (void)e; save_menu_close_async(); }

/* Save pedalboard flow */
static void do_save_pedalboard(void *ud)
{
    (void)ud;
    ui_pedalboard_save();
    ui_app_show_message(TR(TR_SAVED), TR(TR_MSG_PB_SAVED), 2000);
}
static void confirm_save_pedalboard_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    ui_app_show_confirm(TR(TR_MENU_SAVE_PB),
                        TR(TR_CONFIRM_SAVE_PB),
                        do_save_pedalboard, NULL);
}

/* Save snapshot flow */
static void do_save_snapshot(void *ud)
{
    (void)ud;
    ui_pedalboard_save_snapshot();
    ui_app_show_message(TR(TR_SAVED), TR(TR_MSG_SNAP_SAVED), 2000);
}
static void confirm_save_snapshot_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();

    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || pb->current_snapshot < 0) {
        ui_app_show_message(TR(TR_MSG_NO_SNAP), TR(TR_MSG_SELECT_SNAP_FIRST), 2000);
        return;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), TR(TR_CONFIRM_SAVE_SNAP),
             pb->snapshots[pb->current_snapshot].name);
    ui_app_show_confirm(TR(TR_MENU_SAVE_SNAP), msg, do_save_snapshot, NULL);
}

/* Save as flow */
static void do_save_as_input(const char *name, void *ud)
{
    (void)ud;
    if (!name || !name[0]) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;

    mpt_settings_t *s = settings_get();
    char new_dir[PB_PATH_MAX];
    snprintf(new_dir, sizeof(new_dir), "%s/%s.pedalboard", s->pedalboards_dir, name);

    /* Update name before saving so the TTL reflects the new name */
    snprintf(pb->name, sizeof(pb->name), "%s", name);

    if (pb_save_as(pb, new_dir) == 0) {
        ui_app_update_title(pb->name, false);
        ui_pedalboard_save_last_state();
        ui_app_show_toast(TR(TR_MSG_PB_SAVED));
    } else {
        ui_app_show_toast_error(TR(TR_MSG_PB_SAVE_ERROR));
    }
}
static void save_as_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    pedalboard_t *pb = ui_pedalboard_get();
    const char *current_name = pb ? pb->name : "";
    ui_app_show_input("Save pedalboard as...", current_name, do_save_as_input, NULL);
}

/* Delete snapshot flow */
static void do_delete_snapshot(void *ud)
{
    (void)ud;
    ui_pedalboard_delete_snapshot();
    ui_app_show_message(TR(TR_DELETED), TR(TR_MSG_SNAP_DELETED), 2000);
}
static void confirm_delete_snapshot_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || pb->current_snapshot < 0) {
        ui_app_show_message(TR(TR_MSG_NO_SNAP), TR(TR_MSG_SELECT_SNAP_FIRST), 2000);
        return;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), TR(TR_CONFIRM_DELETE_SNAP),
             pb->snapshots[pb->current_snapshot].name);
    ui_app_show_confirm(TR(TR_SNAP_DELETE_TITLE), msg, do_delete_snapshot, NULL);
}

/* Delete pedalboard flow */
static void do_delete_pedalboard(void *ud)
{
    (void)ud;
    ui_pedalboard_delete();
    ui_app_show_message(TR(TR_DELETED), TR(TR_MSG_PB_DELETED), 2000);
}
static void confirm_delete_pedalboard_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded()) {
        ui_app_show_message(TR(TR_MSG_NO_PB_LOADED_TITLE), TR(TR_MSG_NO_PB_LOADED), 2000);
        return;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), TR(TR_CONFIRM_DELETE_PB), pb->name);
    ui_app_show_confirm(TR(TR_MENU_DELETE_PB), msg, do_delete_pedalboard, NULL);
}

static void btn_save_cb(lv_event_t *e)
{
    (void)e;
    if (g_save_menu) { save_menu_close(); return; }

    /* Build a small dropdown near the save button */
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_event_cb(overlay, save_menu_overlay_cb, LV_EVENT_CLICKED, NULL);
    g_save_menu = overlay;

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 320, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_TOP_RIGHT, -8, UI_TOP_BAR_H + 4);
    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *b1 = lv_btn_create(panel);
    lv_obj_set_size(b1, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(b1, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(b1, 4, 0);
    lv_obj_t *l1 = lv_label_create(b1);
    lv_label_set_text_fmt(l1, LV_SYMBOL_SAVE " %s", TR(TR_MENU_SAVE_PB));
    lv_obj_set_style_text_color(l1, UI_COLOR_TEXT, 0);
    lv_obj_center(l1);
    lv_obj_add_event_cb(b1, confirm_save_pedalboard_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *b2 = lv_btn_create(panel);
    lv_obj_set_size(b2, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(b2, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(b2, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(b2, 1, 0);
    lv_obj_set_style_radius(b2, 4, 0);
    lv_obj_t *l2 = lv_label_create(b2);
    lv_label_set_text_fmt(l2, LV_SYMBOL_SAVE " %s", TR(TR_MENU_SAVE_PB_AS));
    lv_obj_set_style_text_color(l2, UI_COLOR_TEXT, 0);
    lv_obj_center(l2);
    lv_obj_add_event_cb(b2, save_as_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *b3 = lv_btn_create(panel);
    lv_obj_set_size(b3, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(b3, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(b3, 4, 0);
    lv_obj_t *l3 = lv_label_create(b3);
    lv_label_set_text_fmt(l3, LV_SYMBOL_DOWNLOAD " %s", TR(TR_MENU_SAVE_SNAP));
    lv_obj_set_style_text_color(l3, UI_COLOR_TEXT, 0);
    lv_obj_center(l3);
    lv_obj_add_event_cb(b3, confirm_save_snapshot_cb, LV_EVENT_CLICKED, NULL);

    lv_color_t danger = lv_color_hex(0xC0392B);

    lv_obj_t *b4 = lv_btn_create(panel);
    lv_obj_set_size(b4, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(b4, danger, 0);
    lv_obj_set_style_radius(b4, 4, 0);
    lv_obj_t *l4 = lv_label_create(b4);
    lv_label_set_text_fmt(l4, LV_SYMBOL_TRASH " %s", TR(TR_MENU_DELETE_SNAP));
    lv_obj_set_style_text_color(l4, UI_COLOR_TEXT, 0);
    lv_obj_center(l4);
    lv_obj_add_event_cb(b4, confirm_delete_snapshot_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *b5 = lv_btn_create(panel);
    lv_obj_set_size(b5, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(b5, danger, 0);
    lv_obj_set_style_radius(b5, 4, 0);
    lv_obj_t *l5 = lv_label_create(b5);
    lv_label_set_text_fmt(l5, LV_SYMBOL_TRASH " %s", TR(TR_MENU_DELETE_PB));
    lv_obj_set_style_text_color(l5, UI_COLOR_TEXT, 0);
    lv_obj_center(l5);
    lv_obj_add_event_cb(b5, confirm_delete_pedalboard_cb, LV_EVENT_CLICKED, NULL);
}

static void btn_settings_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_SETTINGS);
}

/* ─── Top bar creation ───────────────────────────────────────────────────────── */

/* Helper: create a square top-bar button with icon on top and label below. */
static lv_obj_t *make_top_btn(lv_obj_t *parent,
                               const char *icon, const char *label_txt,
                               lv_color_t bg, lv_event_cb_t cb,
                               lv_obj_t **label_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, UI_TOP_BTN_SZ, UI_TOP_BTN_SZ);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 6, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ico = lv_label_create(btn);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, lv_color_white(), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_txt);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

    if (label_out) *label_out = lbl;
    return btn;
}

static void create_top_bar(void)
{
    g_top_bar = lv_obj_create(g_screen);
    lv_obj_set_size(g_top_bar, LV_PCT(100), UI_TOP_BAR_H);
    lv_obj_align(g_top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(g_top_bar, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(g_top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_top_bar, 0, 0);
    lv_obj_set_style_radius(g_top_bar, 0, 0);
    lv_obj_set_style_pad_all(g_top_bar, 5, 0);
    lv_obj_clear_flag(g_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Left side: Banks + Conductor ── */
    lv_obj_t *btn_banks = make_top_btn(g_top_bar,
        LV_SYMBOL_LIST, TR(TR_BANKS),
        UI_COLOR_PRIMARY, btn_banks_cb, &g_banks_label);
    lv_obj_align(btn_banks, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *btn_pre_fx = make_top_btn(g_top_bar,
        LV_SYMBOL_EDIT, TR(TR_PREFX_TITLE),
        lv_color_hex(0x1A7A4A), btn_pre_fx_cb, NULL);
    lv_obj_align(btn_pre_fx, LV_ALIGN_LEFT_MID, UI_TOP_BTN_SZ + 6, 0);

    lv_obj_t *btn_cond = make_top_btn(g_top_bar,
        LV_SYMBOL_AUDIO, TR(TR_CONDUCTOR_TITLE),
        lv_color_hex(0x6A4C9C), btn_conductor_cb, NULL);
    lv_obj_align(btn_cond, LV_ALIGN_LEFT_MID, (UI_TOP_BTN_SZ + 6) * 2, 0);

    /* ── Center: pedalboard title ── */
    g_title_label = lv_label_create(g_top_bar);
    lv_label_set_text(g_title_label, TR(TR_NO_PEDALBOARD));
    lv_obj_set_style_text_color(g_title_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_20, 0);
    lv_label_set_long_mode(g_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_title_label, 680);
    lv_obj_set_style_text_align(g_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_title_label, LV_ALIGN_CENTER, 0, -10);

    /* Modified indicator ("*") */
    g_mod_dot = lv_label_create(g_top_bar);
    lv_label_set_text(g_mod_dot, "*");
    lv_obj_set_style_text_color(g_mod_dot, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(g_mod_dot, &lv_font_montserrat_20, 0);
    lv_obj_align_to(g_mod_dot, g_title_label, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_add_flag(g_mod_dot, LV_OBJ_FLAG_HIDDEN);

    /* ── Right side: Save + Settings ── */
    lv_obj_t *btn_set = make_top_btn(g_top_bar,
        LV_SYMBOL_SETTINGS, TR(TR_SETTINGS_TITLE),
        UI_COLOR_SURFACE, btn_settings_cb, NULL);
    lv_obj_set_style_border_color(btn_set, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(btn_set, 1, 0);
    lv_obj_align(btn_set, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *btn_save = make_top_btn(g_top_bar,
        LV_SYMBOL_SAVE, TR(TR_MENU_SAVE_PB),
        UI_COLOR_ACCENT, btn_save_cb, NULL);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, -(UI_TOP_BTN_SZ + 6), 0);
}

/* ─── Modal helpers ──────────────────────────────────────────────────────────── */

typedef struct {
    ui_confirm_cb_t cb;
    void           *ud;
    lv_obj_t       *overlay;
} confirm_ctx_t;

static void confirm_ok_cb(lv_event_t *e)
{
    confirm_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(ctx->overlay);
    ui_confirm_cb_t cb = ctx->cb;
    void           *ud = ctx->ud;
    free(ctx);
    if (cb) cb(ud);
}

static void confirm_cancel_cb(lv_event_t *e)
{
    confirm_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(ctx->overlay);
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
    /* Copy text before anything is deleted */
    const char *text = lv_textarea_get_text(ctx->ta);
    char text_copy[512];
    snprintf(text_copy, sizeof(text_copy), "%s", text ? text : "");
    lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(ctx->kbd, NULL);
    lv_obj_delete_async(ctx->overlay);
    ui_input_cb_t cb = ctx->cb;
    void          *ud = ctx->ud;
    free(ctx);
    if (cb) cb(text_copy, ud);
}

static void input_cancel_cb(lv_event_t *e)
{
    input_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(ctx->kbd, NULL);
    lv_obj_delete_async(ctx->overlay);
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

    /* Floating "add plugin" button — fixed in top-right of canvas, always on top */
    g_btn_add_float = lv_btn_create(g_screen);
    lv_obj_set_size(g_btn_add_float, 64, 64);
    /* Position: top-right of canvas, 12px from edges */
    lv_obj_set_pos(g_btn_add_float, 1280 - 64 - 12, UI_TOP_BAR_H + 12);
    lv_obj_set_style_bg_color(g_btn_add_float, UI_COLOR_ACTIVE, 0);
    lv_obj_set_style_radius(g_btn_add_float, 32, 0);  /* circle */
    lv_obj_set_style_shadow_width(g_btn_add_float, 8, 0);
    lv_obj_set_style_shadow_color(g_btn_add_float, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_btn_add_float, LV_OPA_40, 0);
    lv_obj_add_event_cb(g_btn_add_float, btn_add_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_add_float = lv_label_create(g_btn_add_float);
    lv_label_set_text(lbl_add_float, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(lbl_add_float, &lv_font_montserrat_28, 0);
    lv_obj_center(lbl_add_float);

    /* Initialize sub-views */
    ui_pedalboard_init(g_content_area);
    ui_snapshot_bar_init(g_snap_bar);

    lv_screen_load(g_screen);
}

void ui_app_show_screen(ui_screen_t screen)
{
    /* Dismiss any open overlays on lv_layer_top() before switching screens.
     * These overlays are not children of g_content_area so lv_obj_clean()
     * alone would leave them orphaned. */
    save_menu_close();
    ui_snapshot_bar_dismiss();
    /* Cancel any active toast: lv_obj_clean() below will destroy g_toast,
     * so we must NULL it first to prevent the stale timer callback from
     * calling lv_obj_del() on the already-freed object (→ SIGSEGV). */
    if (g_toast_timer) { lv_timer_del(g_toast_timer); g_toast_timer = NULL; }
    g_toast = NULL;
    /* Null pointers before lv_obj_clean destroys overlays. */
    ui_conductor_close();
    ui_pre_fx_close();
    /* Clean any remaining overlays (confirm/input dialogs, context menus).
     * All objects on lv_layer_top() are app-owned modals — safe to clear. */
    lv_obj_clean(lv_layer_top());

    lv_obj_clean(g_content_area);

    g_current_screen = screen;
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

    /* Show the floating + button only on the pedalboard view */
    if (g_btn_add_float) {
        if (screen == UI_SCREEN_PEDALBOARD)
            lv_obj_clear_flag(g_btn_add_float, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_btn_add_float, LV_OBJ_FLAG_HIDDEN);
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

/* ── Toast notification ──────────────────────────────────────────────────────── */

static void toast_timer_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    g_toast_timer = NULL;
    if (g_toast) { lv_obj_del(g_toast); g_toast = NULL; }
}

static void show_toast_colored(const char *msg, lv_color_t bg)
{
    if (g_toast_timer) { lv_timer_del(g_toast_timer); g_toast_timer = NULL; }
    if (g_toast)       { lv_obj_del(g_toast);          g_toast = NULL; }

    g_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_toast, 700, 46);
    lv_obj_align(g_toast, LV_ALIGN_TOP_MID, 0, UI_TOP_BAR_H + 8);
    lv_obj_set_style_bg_color(g_toast, bg, 0);
    lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_toast, 6, 0);
    lv_obj_set_style_border_width(g_toast, 0, 0);
    lv_obj_set_style_shadow_width(g_toast, 0, 0);
    lv_obj_set_style_pad_all(g_toast, 0, 0);
    lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(g_toast);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);

    g_toast_timer = lv_timer_create(toast_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(g_toast_timer, 1);
}

void ui_app_show_toast(const char *msg)
{
    show_toast_colored(msg, UI_COLOR_PRIMARY);
}

void ui_app_show_toast_error(const char *msg)
{
    show_toast_colored(msg, lv_color_hex(0xCC2222));
}

void ui_app_show_message(const char *title, const char *body, int autodismiss_ms)
{
    (void)title;
    (void)autodismiss_ms;
    ui_app_show_toast(body);
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
    lv_label_set_text(lbl_cancel, TR(TR_CANCEL));
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, input_cancel_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *btn_ok = lv_btn_create(row);
    lv_obj_set_size(btn_ok, 100, 36);
    lv_obj_set_style_bg_color(btn_ok, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, TR(TR_OK));
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
    lv_label_set_text(lbl_cancel, TR(TR_CANCEL));
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, confirm_cancel_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *btn_ok = lv_btn_create(row);
    lv_obj_set_size(btn_ok, 100, 40);
    lv_obj_set_style_bg_color(btn_ok, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, TR(TR_OK));
    lv_obj_center(lbl_ok);
    lv_obj_add_event_cb(btn_ok, confirm_ok_cb, LV_EVENT_CLICKED, ctx);
}

lv_obj_t *ui_app_content_area(void) { return g_content_area; }

void ui_app_apply_language(void)
{
    /* Update persistent top-bar label */
    if (g_banks_label)
        lv_label_set_text(g_banks_label, TR(TR_BANKS));

    /* Update snapshot bar prefix */
    ui_snapshot_bar_update_lang();

    /* Rebuild the current content screen with new strings */
    ui_app_show_screen(g_current_screen);
}
