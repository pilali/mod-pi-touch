#include "ui_bank_browser.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../bank.h"
#include "../settings.h"
#include "../pedalboard.h"
#include "../i18n.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── Layout ─────────────────────────────────────────────────────────────── */
#define LEFT_W      280   /* left column width */
#define HEADER_H     44   /* header row height */
#define ROW_H        50   /* bank / pedalboard row height */
#define BTN_H        44   /* action button height */
#define DRAG_LONG_MS 500  /* long-press threshold for drag (ms) */

/* ─── State ──────────────────────────────────────────────────────────────── */
static bank_list_t  g_banks;
static int          g_selected_bank   = -1;   /* -1 = Tous */

static lv_obj_t    *g_left_scroll     = NULL;
static lv_obj_t    *g_right_scroll    = NULL;
static lv_obj_t    *g_right_add_btn   = NULL; /* "+" button, shown in bank mode */

/* All pedalboard paths (pb_list result) — freed on browser close */
static char        *g_pb_paths[256];
static int          g_pb_count        = 0;

/* Row objects in right panel (bank mode — for drag position lookup) */
static lv_obj_t    *g_pedal_rows[BANK_MAX_PEDALS];
static int          g_pedal_row_count = 0;

/* Drag state */
static bool         g_drag_active     = false;
static int          g_drag_src        = -1;
static int          g_drag_dst        = -1;
static lv_obj_t    *g_drag_ghost      = NULL;

/* Multi-select overlay */
static lv_obj_t    *g_sel_overlay     = NULL;
static bool         g_sel_flags[256];
static lv_obj_t    *g_sel_btns[256];
static int          g_sel_count       = 0;

/* Pending state for confirmation dialogs */
static char         g_pending_bundle[PB_PATH_MAX];
static int          g_pending_pedal   = -1;   /* pedal index to remove */
static int          g_pending_bank    = -1;   /* bank index to delete  */

/* ─── Forward declarations ───────────────────────────────────────────────── */
static void refresh_right(void);
static void refresh_left(void);

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

static void save_banks(void)
{
    mpt_settings_t *s = settings_get();
    bank_save(s->banks_file, &g_banks);
}

static void free_pb_paths(void)
{
    for (int i = 0; i < g_pb_count; i++) { free(g_pb_paths[i]); g_pb_paths[i] = NULL; }
    g_pb_count = 0;
}

/* Return display name for a bundle path (strip dir + ".pedalboard" suffix). */
static const char *pb_name_from_path(const char *path)
{
    static char buf[PB_NAME_MAX];
    const char *sl = strrchr(path, '/');
    snprintf(buf, sizeof(buf), "%s", sl ? sl + 1 : path);
    char *dot = strrchr(buf, '.');
    if (dot) *dot = '\0';
    return buf;
}

/* ─── Load confirm (before switching pedalboard) ──────────────────────────── */

static void do_load(bool save_first)
{
    if (save_first) ui_pedalboard_save();
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
    ui_pedalboard_load(g_pending_bundle, NULL, NULL);
}

static void save_and_load_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_delete_async(overlay);
    do_load(true);
}

static void discard_and_load_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_delete_async(overlay);
    do_load(false);
}

static void cancel_load_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_delete_async(overlay);
}

static void show_load_confirm(const char *name)
{
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_center(overlay);

    lv_obj_t *box = lv_obj_create(overlay);
    lv_obj_set_size(box, 560, LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(box, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(box, 20, 0);
    lv_obj_set_style_pad_row(box, 14, 0);

    bool has_unsaved = ui_pedalboard_is_loaded() && ui_pedalboard_get()->modified;
    char msg[PB_NAME_MAX + 80];
    if (has_unsaved)
        snprintf(msg, sizeof(msg), "Charger \"%s\" ?\n\nModifications non sauvegardées sur \"%s\".",
                 name, ui_pedalboard_get()->name);
    else
        snprintf(msg, sizeof(msg), "Charger \"%s\" ?", name);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

    lv_obj_t *row = lv_obj_create(box);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);

    if (has_unsaved) {
        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_set_size(btn, 160, BTN_H);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, 0);
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, LV_SYMBOL_SAVE " Sauver & Charger");
        lv_obj_center(l);
        lv_obj_add_event_cb(btn, save_and_load_cb, LV_EVENT_CLICKED, overlay);
    }

    lv_obj_t *btn_load = lv_btn_create(row);
    lv_obj_set_size(btn_load, 110, BTN_H);
    lv_obj_set_style_bg_color(btn_load, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_l = lv_label_create(btn_load);
    lv_label_set_text(lbl_l, has_unsaved ? "Charger" : LV_SYMBOL_RIGHT " Charger");
    lv_obj_center(lbl_l);
    lv_obj_add_event_cb(btn_load, discard_and_load_cb, LV_EVENT_CLICKED, overlay);

    lv_obj_t *btn_cancel = lv_btn_create(row);
    lv_obj_set_size(btn_cancel, 100, BTN_H);
    lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, TR(TR_CANCEL));
    lv_obj_center(lbl_c);
    lv_obj_add_event_cb(btn_cancel, cancel_load_cb, LV_EVENT_CLICKED, overlay);
}

/* ─── Right panel — pedalboard list ──────────────────────────────────────── */

static void pedal_load_cb(lv_event_t *e)
{
    const char *bundle = lv_event_get_user_data(e);
    if (!bundle) return;
    snprintf(g_pending_bundle, sizeof(g_pending_bundle), "%s", bundle);
    show_load_confirm(pb_name_from_path(bundle));
}

/* ── Drag & drop reorder ── */

static int find_drop_slot(lv_coord_t touch_y)
{
    for (int i = 0; i < g_pedal_row_count; i++) {
        lv_area_t area;
        lv_obj_get_coords(g_pedal_rows[i], &area);
        if (touch_y < (area.y1 + area.y2) / 2) return i;
    }
    return g_pedal_row_count > 0 ? g_pedal_row_count - 1 : 0;
}

static void drag_end(void)
{
    if (!g_drag_active) return;
    g_drag_active = false;

    /* Restore target row highlight */
    if (g_drag_dst >= 0 && g_drag_dst < g_pedal_row_count)
        lv_obj_set_style_bg_color(g_pedal_rows[g_drag_dst], UI_COLOR_SURFACE, 0);
    /* Restore source row opacity */
    if (g_drag_src >= 0 && g_drag_src < g_pedal_row_count)
        lv_obj_set_style_opa(g_pedal_rows[g_drag_src], LV_OPA_COVER, 0);

    if (g_drag_ghost) { lv_obj_delete_async(g_drag_ghost); g_drag_ghost = NULL; }

    /* Restore list scrollability */
    if (g_right_scroll) lv_obj_add_flag(g_right_scroll, LV_OBJ_FLAG_SCROLLABLE);

    if (g_drag_src != g_drag_dst && g_drag_src >= 0 && g_selected_bank >= 0) {
        bank_move_pedal(&g_banks, g_selected_bank, g_drag_src, g_drag_dst);
        save_banks();
        /* refresh_right() is defined below — forward-declared as static */
        refresh_right();
    }
    g_drag_src = g_drag_dst = -1;
}

static void drag_long_press_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_drag_active || g_selected_bank < 0) return;
    if (idx < 0 || idx >= g_pedal_row_count) return;

    g_drag_active = true;
    g_drag_src    = idx;
    g_drag_dst    = idx;

    /* Freeze list scroll while dragging */
    if (g_right_scroll) lv_obj_clear_flag(g_right_scroll, LV_OBJ_FLAG_SCROLLABLE);

    /* Dim source row */
    lv_obj_set_style_opa(g_pedal_rows[idx], LV_OPA_40, 0);

    /* Create ghost */
    bank_t *bank = &g_banks.banks[g_selected_bank];
    lv_indev_t *indev = lv_indev_active();
    lv_point_t pt = {0, 0};
    if (indev) lv_indev_get_point(indev, &pt);

    g_drag_ghost = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_drag_ghost, 960, ROW_H - 4);
    lv_obj_set_pos(g_drag_ghost, LEFT_W + 4, pt.y - (ROW_H / 2));
    lv_obj_set_style_bg_color(g_drag_ghost, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(g_drag_ghost, LV_OPA_80, 0);
    lv_obj_set_style_border_width(g_drag_ghost, 0, 0);
    lv_obj_set_style_radius(g_drag_ghost, 6, 0);
    lv_obj_add_flag(g_drag_ghost, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(g_drag_ghost, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ghost_lbl = lv_label_create(g_drag_ghost);
    lv_label_set_text(ghost_lbl, bank->pedals[idx].title);
    lv_obj_set_style_text_color(ghost_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ghost_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(ghost_lbl, LV_ALIGN_LEFT_MID, 36, 0);
}

static void drag_pressing_cb(lv_event_t *e)
{
    (void)e;
    if (!g_drag_active) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    /* Move ghost */
    if (g_drag_ghost)
        lv_obj_set_pos(g_drag_ghost, LEFT_W + 4, pt.y - (ROW_H / 2));

    /* Update drop target highlight */
    int new_dst = find_drop_slot(pt.y);
    if (new_dst != g_drag_dst) {
        if (g_drag_dst >= 0 && g_drag_dst < g_pedal_row_count)
            lv_obj_set_style_bg_color(g_pedal_rows[g_drag_dst], UI_COLOR_SURFACE, 0);
        g_drag_dst = new_dst;
        if (g_drag_dst >= 0 && g_drag_dst < g_pedal_row_count && g_drag_dst != g_drag_src)
            lv_obj_set_style_bg_color(g_pedal_rows[g_drag_dst], UI_COLOR_ACCENT, 0);
    }
}

static void drag_released_cb(lv_event_t *e)
{
    (void)e;
    drag_end();
}

/* ── Remove pedal from bank ── */

static void do_remove_pedal_confirmed(void *ud)
{
    (void)ud;
    if (g_pending_bank < 0 || g_pending_pedal < 0) return;
    bank_remove_pedal(&g_banks, g_pending_bank, g_pending_pedal);
    save_banks();
    refresh_right();
    g_pending_bank = g_pending_pedal = -1;
}

static void remove_pedal_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_selected_bank < 0 || idx < 0) return;
    bank_t *bank = &g_banks.banks[g_selected_bank];
    if (idx >= bank->pedal_count) return;

    g_pending_bank  = g_selected_bank;
    g_pending_pedal = idx;

    char msg[PB_NAME_MAX + 64];
    snprintf(msg, sizeof(msg), TR(TR_BANK_REMOVE_PB_MSG), bank->pedals[idx].title);
    ui_app_show_confirm(TR(TR_BANK_REMOVE_PB_TITLE), msg,
                        do_remove_pedal_confirmed, NULL);
}

/* ── Multi-select overlay (add pedalboards to bank) ── */

static void sel_update_btn_style(int i)
{
    if (!g_sel_btns[i]) return;
    lv_obj_set_style_bg_color(g_sel_btns[i],
        g_sel_flags[i] ? lv_color_hex(0x27AE60) : UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(g_sel_btns[i],
        g_sel_flags[i] ? lv_color_hex(0x27AE60) : UI_COLOR_SURFACE, 0);
}

static void sel_toggle_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_sel_flags[idx] = !g_sel_flags[idx];
    sel_update_btn_style(idx);
}

static void sel_validate_cb(lv_event_t *e)
{
    (void)e;
    if (g_selected_bank < 0) goto close;
    mpt_settings_t *s = settings_get();
    for (int i = 0; i < g_sel_count; i++) {
        if (!g_sel_flags[i] || !g_pb_paths[i]) continue;
        char pb_title[PB_NAME_MAX];
        pb_read_name(g_pb_paths[i], pb_title, sizeof(pb_title));
        /* Resolve symlinks so the stored path matches what mod-ui writes */
        char real_bundle[PB_PATH_MAX];
        const char *bundle = realpath(g_pb_paths[i], real_bundle) ? real_bundle : g_pb_paths[i];
        bank_add_pedal(&g_banks, g_banks.banks[g_selected_bank].name,
                       pb_title, bundle);
        (void)s;
    }
    save_banks();
    refresh_right();
close:
    if (g_sel_overlay) { lv_obj_delete_async(g_sel_overlay); g_sel_overlay = NULL; }
}

static void sel_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (g_sel_overlay) { lv_obj_delete_async(g_sel_overlay); g_sel_overlay = NULL; }
}

static void open_add_overlay(void)
{
    if (g_sel_overlay || g_selected_bank < 0) return;

    /* Load all pedalboards */
    free_pb_paths();
    mpt_settings_t *s = settings_get();
    g_pb_count = pb_list(s->pedalboards_dir, g_pb_paths, 256);
    g_sel_count = g_pb_count;
    memset(g_sel_flags, 0, sizeof(g_sel_flags));
    memset(g_sel_btns,  0, sizeof(g_sel_btns));

    /* Overlay background */
    g_sel_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_sel_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_sel_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_sel_overlay, LV_OPA_80, 0);
    lv_obj_set_flex_flow(g_sel_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_sel_overlay, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(g_sel_overlay, 24, 0);
    lv_obj_set_style_pad_row(g_sel_overlay, 12, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(g_sel_overlay);
    lv_label_set_text_fmt(title, "%s — %s", TR(TR_BANK_ADD_PB),
                          g_banks.banks[g_selected_bank].name);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    /* Scrollable list of PBs */
    lv_obj_t *list = lv_obj_create(g_sel_overlay);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_column(list, 6, 0);

    for (int i = 0; i < g_pb_count; i++) {
        char pb_title[PB_NAME_MAX];
        pb_read_name(g_pb_paths[i], pb_title, sizeof(pb_title));
        lv_obj_t *btn = lv_btn_create(list);
        lv_obj_set_height(btn, BTN_H);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(btn, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, pb_title);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, sel_toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_sel_btns[i] = btn;
    }

    /* Button row */
    lv_obj_t *row = lv_obj_create(g_sel_overlay);
    lv_obj_set_size(row, LV_PCT(100), BTN_H + 8);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);

    lv_obj_t *btn_cancel = lv_btn_create(row);
    lv_obj_set_size(btn_cancel, 130, BTN_H);
    lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_BYPASS, 0);
    lv_obj_t *lc = lv_label_create(btn_cancel);
    lv_label_set_text(lc, TR(TR_CANCEL));
    lv_obj_center(lc);
    lv_obj_add_event_cb(btn_cancel, sel_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ok = lv_btn_create(row);
    lv_obj_set_size(btn_ok, 160, BTN_H);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x27AE60), 0);
    lv_obj_t *lok = lv_label_create(btn_ok);
    lv_label_set_text_fmt(lok, LV_SYMBOL_OK " %s", TR(TR_BANK_VALIDATE));
    lv_obj_center(lok);
    lv_obj_add_event_cb(btn_ok, sel_validate_cb, LV_EVENT_CLICKED, NULL);
}

static void open_add_cb(lv_event_t *e)
{
    (void)e;
    open_add_overlay();
}

/* ─── Right panel builder ─────────────────────────────────────────────────── */

static void refresh_right(void)
{
    if (!g_right_scroll) return;

    /* Cancel any ongoing drag */
    if (g_drag_active) {
        g_drag_active = false;
        if (g_drag_ghost) { lv_obj_delete_async(g_drag_ghost); g_drag_ghost = NULL; }
        g_drag_src = g_drag_dst = -1;
    }

    lv_obj_clean(g_right_scroll);
    g_pedal_row_count = 0;
    memset(g_pedal_rows, 0, sizeof(g_pedal_rows));

    if (g_selected_bank == -1) {
        /* ── Tous : simple list, tap to load ── */
        free_pb_paths();
        mpt_settings_t *s = settings_get();
        g_pb_count = pb_list(s->pedalboards_dir, g_pb_paths, 256);

        if (g_pb_count == 0) {
            lv_obj_t *msg = lv_label_create(g_right_scroll);
            lv_label_set_text(msg, TR(TR_BANK_NO_PB_FOUND));
            lv_obj_set_style_text_color(msg, UI_COLOR_TEXT_DIM, 0);
            lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
            lv_obj_set_style_pad_all(msg, 20, 0);
        }

        for (int i = 0; i < g_pb_count; i++) {
            char pb_title[PB_NAME_MAX];
            pb_read_name(g_pb_paths[i], pb_title, sizeof(pb_title));
            lv_obj_t *btn = lv_obj_create(g_right_scroll);
            lv_obj_set_size(btn, LV_PCT(100), ROW_H);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, pb_title);
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);

            lv_obj_add_event_cb(btn, pedal_load_cb, LV_EVENT_CLICKED, g_pb_paths[i]);
        }

        /* Hide add button in "Tous" mode */
        if (g_right_add_btn) lv_obj_add_flag(g_right_add_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* ── Bank mode : draggable rows with trash ── */
    bank_t *bank = &g_banks.banks[g_selected_bank];

    if (bank->pedal_count == 0) {
        lv_obj_t *msg = lv_label_create(g_right_scroll);
        lv_label_set_text(msg, TR(TR_BANK_NO_PB_IN_BANK));
        lv_obj_set_style_text_color(msg, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
        lv_obj_set_style_pad_all(msg, 20, 0);
    }

    for (int i = 0; i < bank->pedal_count && i < BANK_MAX_PEDALS; i++) {
        /* Row container */
        lv_obj_t *row = lv_obj_create(g_right_scroll);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        g_pedal_rows[g_pedal_row_count++] = row;

        /* Drag handle */
        lv_obj_t *handle = lv_label_create(row);
        lv_label_set_text(handle, LV_SYMBOL_LIST);
        lv_obj_set_size(handle, 44, ROW_H);
        lv_obj_set_style_text_color(handle, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(handle, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(handle, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(handle, drag_long_press_cb, LV_EVENT_LONG_PRESSED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(handle, drag_pressing_cb,   LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(handle, drag_released_cb,   LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(handle, drag_released_cb,   LV_EVENT_PRESS_LOST, NULL);

        /* Pedalboard name (also tappable to load) */
        lv_obj_t *name_btn = lv_obj_create(row);
        lv_obj_set_height(name_btn, ROW_H);
        lv_obj_set_flex_grow(name_btn, 1);
        lv_obj_set_style_bg_opa(name_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(name_btn, 0, 0);
        lv_obj_set_style_pad_all(name_btn, 0, 0);
        lv_obj_add_flag(name_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(name_btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(name_btn);
        lv_label_set_text(lbl, bank->pedals[i].title);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, LV_PCT(100));

        lv_obj_add_event_cb(name_btn, pedal_load_cb, LV_EVENT_CLICKED,
                            bank->pedals[i].bundle);

        /* Trash button */
        lv_obj_t *trash = lv_btn_create(row);
        lv_obj_set_size(trash, 52, ROW_H);
        lv_obj_set_style_bg_color(trash, lv_color_hex(0xC0392B), 0);
        lv_obj_set_style_bg_opa(trash, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(trash, 0, 0);
        lv_obj_set_style_radius(trash, 4, 0);
        lv_obj_t *trash_lbl = lv_label_create(trash);
        lv_label_set_text(trash_lbl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(trash_lbl, lv_color_hex(0xC0392B), 0);
        lv_obj_set_style_text_font(trash_lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(trash_lbl);
        lv_obj_add_event_cb(trash, remove_pedal_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    /* Show "+" add button in bank mode */
    if (g_right_add_btn) lv_obj_clear_flag(g_right_add_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ─── Left panel — bank list ─────────────────────────────────────────────── */

static void refresh_left(void);

static void bank_select_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_selected_bank = idx;
    refresh_left();
    refresh_right();
}

static void do_delete_bank_confirmed(void *ud)
{
    (void)ud;
    if (g_pending_bank < 0) return;
    bank_delete(&g_banks, g_pending_bank);
    save_banks();
    /* If we deleted the currently selected bank, fall back to "Tous" */
    if (g_selected_bank >= g_banks.bank_count)
        g_selected_bank = g_banks.bank_count > 0 ? g_banks.bank_count - 1 : -1;
    else if (g_selected_bank == g_pending_bank)
        g_selected_bank = -1;
    g_pending_bank = -1;
    refresh_left();
    refresh_right();
}

static void delete_bank_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_banks.bank_count) return;
    g_pending_bank = idx;
    char msg[BANK_NAME_MAX + 128];
    snprintf(msg, sizeof(msg), TR(TR_BANK_CONFIRM_DELETE_MSG),
             g_banks.banks[idx].name);
    ui_app_show_confirm(TR(TR_BANK_CONFIRM_DELETE_TITLE), msg,
                        do_delete_bank_confirmed, NULL);
}

static void do_create_bank(const char *name, void *ud)
{
    (void)ud;
    if (!name || !name[0]) return;
    int idx = bank_create(&g_banks, name);
    if (idx < 0) return;
    save_banks();
    g_selected_bank = idx;
    refresh_left();
    refresh_right();
}

static void add_bank_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_input(TR(TR_BANK_NEW_BANK), TR(TR_BANK_NEW_BANK_HINT),
                      do_create_bank, NULL);
}

static void refresh_left(void)
{
    if (!g_left_scroll) return;
    lv_obj_clean(g_left_scroll);

    /* ── "Tous" row ── */
    {
        lv_obj_t *row = lv_obj_create(g_left_scroll);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        bool active = (g_selected_bank == -1);
        lv_obj_set_style_bg_color(row, active ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, TR(TR_BANK_ALL));
        lv_obj_set_style_text_color(lbl, active ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_add_event_cb(row, bank_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    }

    /* ── Separator ── */
    {
        lv_obj_t *sep = lv_obj_create(g_left_scroll);
        lv_obj_set_size(sep, LV_PCT(100), 1);
        lv_obj_set_style_bg_color(sep, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_set_style_pad_all(sep, 0, 0);
        lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
    }

    /* ── One row per bank ── */
    for (int bi = 0; bi < g_banks.bank_count; bi++) {
        lv_obj_t *row = lv_obj_create(g_left_scroll);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        bool active = (g_selected_bank == bi);
        lv_obj_set_style_bg_color(row, active ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /* Bank name (clickable area) */
        lv_obj_t *name_area = lv_obj_create(row);
        lv_obj_set_height(name_area, ROW_H);
        lv_obj_set_flex_grow(name_area, 1);
        lv_obj_set_style_bg_opa(name_area, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(name_area, 0, 0);
        lv_obj_set_style_pad_all(name_area, 0, 0);
        lv_obj_add_flag(name_area, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lbl = lv_label_create(name_area);
        lv_label_set_text(lbl, g_banks.banks[bi].name);
        lv_obj_set_style_text_color(lbl, active ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_obj_add_event_cb(name_area, bank_select_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)bi);

        /* Trash button */
        lv_obj_t *trash = lv_btn_create(row);
        lv_obj_set_size(trash, 44, ROW_H);
        lv_obj_set_style_bg_opa(trash, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(trash, 0, 0);
        lv_obj_set_style_radius(trash, 0, 0);
        lv_obj_t *trash_lbl = lv_label_create(trash);
        lv_label_set_text(trash_lbl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(trash_lbl, lv_color_hex(0xC0392B), 0);
        lv_obj_set_style_text_font(trash_lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(trash_lbl);
        lv_obj_add_event_cb(trash, delete_bank_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)bi);
    }
}

/* ─── Back ────────────────────────────────────────────────────────────────── */

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (g_drag_active) drag_end();
    free_pb_paths();
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

/* ─── Public entry point ──────────────────────────────────────────────────── */

void ui_bank_browser_show(lv_obj_t *parent)
{
    /* Reset state */
    g_left_scroll    = NULL;
    g_right_scroll   = NULL;
    g_right_add_btn  = NULL;
    g_pedal_row_count = 0;
    g_drag_active    = false;
    g_drag_ghost     = NULL;
    g_drag_src = g_drag_dst = -1;
    g_sel_overlay    = NULL;
    g_pending_bank = g_pending_pedal = -1;
    free_pb_paths();

    mpt_settings_t *s = settings_get();
    bank_load(s->banks_file, &g_banks);

    /* ── Root layout: column ── */
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);

    /* ── Header ── */
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), HEADER_H);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(hdr, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 6, 0);
    lv_obj_set_style_pad_column(hdr, 10, 0);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 90, 34);
    lv_obj_set_style_bg_color(btn_back, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text_fmt(lbl_back, LV_SYMBOL_LEFT " %s", TR(TR_BACK));
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, TR(TR_BANK_TITLE));
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_20, 0);

    /* ── Two-column body ── */
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_size(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_column(body, 0, 0);

    /* ── Left column (banks) ── */
    lv_obj_t *left = lv_obj_create(body);
    lv_obj_set_size(left, LEFT_W, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(left, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_pad_all(left, 6, 0);
    lv_obj_set_style_pad_row(left, 4, 0);

    /* Scrollable bank list area */
    g_left_scroll = lv_obj_create(left);
    lv_obj_set_size(g_left_scroll, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(g_left_scroll, 1);
    lv_obj_set_flex_flow(g_left_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_left_scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(g_left_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_left_scroll, 0, 0);
    lv_obj_set_style_pad_all(g_left_scroll, 0, 0);
    lv_obj_set_style_pad_row(g_left_scroll, 4, 0);

    /* "+ Banque" button at bottom of left column */
    lv_obj_t *add_bank_btn = lv_btn_create(left);
    lv_obj_set_size(add_bank_btn, LV_PCT(100), BTN_H);
    lv_obj_set_style_bg_color(add_bank_btn, lv_color_hex(0x27AE60), 0);
    lv_obj_set_style_radius(add_bank_btn, 4, 0);
    lv_obj_t *add_bank_lbl = lv_label_create(add_bank_btn);
    lv_label_set_text(add_bank_lbl, TR(TR_BANK_NEW_BANK));
    lv_obj_set_style_text_font(add_bank_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(add_bank_lbl);
    lv_obj_add_event_cb(add_bank_btn, add_bank_cb, LV_EVENT_CLICKED, NULL);

    /* ── Right column (pedalboards) ── */
    lv_obj_t *right = lv_obj_create(body);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(right, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 6, 0);
    lv_obj_set_style_pad_row(right, 4, 0);

    /* Scrollable pedalboard list */
    g_right_scroll = lv_obj_create(right);
    lv_obj_set_size(g_right_scroll, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(g_right_scroll, 1);
    lv_obj_set_flex_flow(g_right_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_right_scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(g_right_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_right_scroll, 0, 0);
    lv_obj_set_style_pad_all(g_right_scroll, 0, 0);
    lv_obj_set_style_pad_row(g_right_scroll, 4, 0);

    /* "+ Ajouter pedalboards" button — hidden in "Tous" mode */
    g_right_add_btn = lv_btn_create(right);
    lv_obj_set_size(g_right_add_btn, LV_PCT(100), BTN_H);
    lv_obj_set_style_bg_color(g_right_add_btn, lv_color_hex(0x27AE60), 0);
    lv_obj_set_style_radius(g_right_add_btn, 4, 0);
    lv_obj_t *add_pb_lbl = lv_label_create(g_right_add_btn);
    lv_label_set_text(add_pb_lbl, TR(TR_BANK_ADD_PB));
    lv_obj_set_style_text_font(add_pb_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(add_pb_lbl);
    lv_obj_add_event_cb(g_right_add_btn, open_add_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(g_right_add_btn, LV_OBJ_FLAG_HIDDEN); /* shown by refresh_right */

    /* Initial fill */
    refresh_left();
    refresh_right();
}
