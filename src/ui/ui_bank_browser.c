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

/* ─── State ─────────────────────────────────────────────────────────────────── */

static bank_list_t g_banks;
static lv_obj_t   *g_list       = NULL;
static lv_obj_t   *g_filter_bar = NULL;
static int         g_selected_bank = -1; /* -1 = show all */

/* Heap paths from pb_list() — freed on reload */
static char *g_pb_paths[256];
static int   g_pb_count = 0;

/* Filter button references for style updates */
#define FILTER_BTNS_MAX (BANK_MAX + 1)
static lv_obj_t *g_filter_btns[FILTER_BTNS_MAX];
static int       g_filter_btn_count = 0;

/* Pending load path (used by save-confirm dialog callbacks) */
static char g_pending_bundle[PB_PATH_MAX];

/* ─── Helpers ────────────────────────────────────────────────────────────────── */

static void free_pb_paths(void)
{
    for (int i = 0; i < g_pb_count; i++) { free(g_pb_paths[i]); g_pb_paths[i] = NULL; }
    g_pb_count = 0;
}

static void update_filter_styles(void)
{
    for (int i = 0; i < g_filter_btn_count; i++) {
        if (!g_filter_btns[i]) continue;
        int bank_idx = i - 1; /* 0 = All (bank_idx -1), 1.. = bank 0,1,.. */
        bool active = (bank_idx == g_selected_bank);
        lv_obj_set_style_bg_color(g_filter_btns[i],
            active ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_text_color(
            lv_obj_get_child(g_filter_btns[i], 0),
            active ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM, 0);
    }
}

/* ─── List refresh ───────────────────────────────────────────────────────────── */

/* ─── Save-confirm dialog helpers ───────────────────────────────────────────── */

static void do_load(bool save_first)
{
    if (save_first)
        ui_pedalboard_save();
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
    ui_pedalboard_load(g_pending_bundle);
}

static void save_and_load_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(overlay);
    do_load(true);
}

static void discard_and_load_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(overlay);
    do_load(false);
}

static void cancel_load_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(overlay);
    /* Stay in bank browser — do nothing */
}

/* Show load confirmation dialog.
 * If current pedalboard has unsaved changes: Save / Load / Cancel.
 * Otherwise: Load / Cancel. */
static void show_load_confirm(const char *new_name)
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
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_style_pad_row(box, 12, 0);

    /* Message */
    char msg[PB_NAME_MAX * 2 + 80];
    bool has_unsaved = ui_pedalboard_is_loaded() && ui_pedalboard_get()->modified;
    if (has_unsaved) {
        snprintf(msg, sizeof(msg),
                 "Load \"%s\"?\n\nUnsaved changes to \"%s\" will be lost.",
                 new_name, ui_pedalboard_get()->name);
    } else {
        snprintf(msg, sizeof(msg), "Load \"%s\"?", new_name);
    }
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

    /* Button row */
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    if (has_unsaved) {
        /* Save first, then load */
        lv_obj_t *btn_save = lv_btn_create(row);
        lv_obj_set_size(btn_save, 140, 44);
        lv_obj_set_style_bg_color(btn_save, UI_COLOR_ACCENT, 0);
        lv_obj_t *lbl_s = lv_label_create(btn_save);
        lv_label_set_text(lbl_s, LV_SYMBOL_SAVE " Save & Load");
        lv_obj_center(lbl_s);
        lv_obj_add_event_cb(btn_save, save_and_load_cb, LV_EVENT_CLICKED, overlay);
    }

    /* Load without saving */
    lv_obj_t *btn_load = lv_btn_create(row);
    lv_obj_set_size(btn_load, has_unsaved ? 120 : 160, 44);
    lv_obj_set_style_bg_color(btn_load, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_l = lv_label_create(btn_load);
    lv_label_set_text(lbl_l, has_unsaved ? "Load" : LV_SYMBOL_RIGHT " Load");
    lv_obj_center(lbl_l);
    lv_obj_add_event_cb(btn_load, discard_and_load_cb, LV_EVENT_CLICKED, overlay);

    /* Cancel */
    lv_obj_t *btn_cancel = lv_btn_create(row);
    lv_obj_set_size(btn_cancel, 120, 44);
    lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Cancel");
    lv_obj_center(lbl_c);
    lv_obj_add_event_cb(btn_cancel, cancel_load_cb, LV_EVENT_CLICKED, overlay);
}

static void pedal_select_cb(lv_event_t *e)
{
    const char *bundle = lv_event_get_user_data(e);
    if (!bundle) return;

    snprintf(g_pending_bundle, sizeof(g_pending_bundle), "%s", bundle);

    /* Extract pedalboard name from bundle path for the dialog */
    const char *sl = strrchr(g_pending_bundle, '/');
    char name[PB_NAME_MAX];
    snprintf(name, sizeof(name), "%s", sl ? sl + 1 : g_pending_bundle);
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';

    /* Always confirm before loading */
    show_load_confirm(name);
}

static void refresh_pedalboard_list(void)
{
    if (!g_list) return;
    lv_obj_clean(g_list);
    free_pb_paths();

    mpt_settings_t *s = settings_get();

    if (g_selected_bank == -1) {
        /* All pedalboards from filesystem */
        g_pb_count = pb_list(s->pedalboards_dir, g_pb_paths, 256);
        if (g_pb_count == 0) {
            lv_obj_t *lbl = lv_label_create(g_list);
            lv_label_set_text(lbl, TR(TR_BANK_NO_PB_FOUND));
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
            return;
        }
        for (int i = 0; i < g_pb_count; i++) {
            const char *sl = strrchr(g_pb_paths[i], '/');
            char name[256];
            snprintf(name, sizeof(name), "%s", sl ? sl + 1 : g_pb_paths[i]);
            char *dot = strrchr(name, '.');
            if (dot) *dot = '\0';
            lv_obj_t *btn = lv_list_add_button(g_list, LV_SYMBOL_AUDIO, name);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, 0);
            lv_obj_add_event_cb(btn, pedal_select_cb, LV_EVENT_CLICKED, g_pb_paths[i]);
        }
    } else {
        /* Pedalboards from selected bank */
        bank_t *bank = &g_banks.banks[g_selected_bank];
        if (bank->pedal_count == 0) {
            lv_obj_t *lbl = lv_label_create(g_list);
            lv_label_set_text(lbl, TR(TR_BANK_NO_PB_IN_BANK));
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
            return;
        }
        for (int i = 0; i < bank->pedal_count; i++) {
            lv_obj_t *btn = lv_list_add_button(g_list, LV_SYMBOL_AUDIO,
                                               bank->pedals[i].title);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, 0);
            lv_obj_add_event_cb(btn, pedal_select_cb, LV_EVENT_CLICKED,
                               bank->pedals[i].bundle);
        }
    }
}

/* ─── Callbacks ──────────────────────────────────────────────────────────────── */

static void filter_cb(lv_event_t *e)
{
    int bank_idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_selected_bank = bank_idx;
    update_filter_styles();
    refresh_pedalboard_list();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    free_pb_paths();
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

/* ─── Public ─────────────────────────────────────────────────────────────────── */

void ui_bank_browser_show(lv_obj_t *parent)
{
    g_list = NULL;
    g_filter_bar = NULL;
    g_filter_btn_count = 0;
    free_pb_paths();

    /* Load banks */
    mpt_settings_t *s = settings_get();
    bank_load(s->banks_file, &g_banks);

    /* Parent layout */
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_set_style_pad_column(parent, 8, 0);

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

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_set_style_bg_color(btn_back, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text_fmt(lbl_back, LV_SYMBOL_LEFT " %s", TR(TR_BACK));
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, TR(TR_BANK_TITLE));
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);

    /* ── Bank filter bar (only if banks exist) ── */
    if (g_banks.bank_count > 0) {
        g_filter_bar = lv_obj_create(parent);
        lv_obj_set_size(g_filter_bar, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(g_filter_bar, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(g_filter_bar, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(g_filter_bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_filter_bar, 0, 0);
        lv_obj_set_style_pad_all(g_filter_bar, 0, 0);
        lv_obj_set_style_pad_row(g_filter_bar, 4, 0);
        lv_obj_set_style_pad_column(g_filter_bar, 4, 0);

        /* "All" button — bank_idx = -1 */
        lv_obj_t *btn_all = lv_btn_create(g_filter_bar);
        lv_obj_set_height(btn_all, 34);
        lv_obj_set_style_bg_color(btn_all, UI_COLOR_PRIMARY, 0);
        lv_obj_t *lbl_all = lv_label_create(btn_all);
        lv_label_set_text(lbl_all, TR(TR_BANK_ALL));
        lv_obj_center(lbl_all);
        lv_obj_add_event_cb(btn_all, filter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
        g_filter_btns[g_filter_btn_count++] = btn_all;

        /* One button per bank */
        for (int bi = 0; bi < g_banks.bank_count && g_filter_btn_count < FILTER_BTNS_MAX; bi++) {
            lv_obj_t *btn = lv_btn_create(g_filter_bar);
            lv_obj_set_height(btn, 34);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, g_banks.banks[bi].name);
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, filter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)bi);
            g_filter_btns[g_filter_btn_count++] = btn;
        }

        update_filter_styles();
    }

    /* ── Pedalboard list ── */
    g_list = lv_list_create(parent);
    lv_obj_set_size(g_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(g_list, 1);
    lv_obj_set_style_bg_color(g_list, UI_COLOR_BG, 0);

    refresh_pedalboard_list();
}
