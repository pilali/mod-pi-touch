#include "ui_bank_browser.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../bank.h"
#include "../settings.h"
#include "../pedalboard.h"

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

static void pedal_select_cb(lv_event_t *e)
{
    const char *bundle = lv_event_get_user_data(e);
    if (!bundle) return;
    ui_pedalboard_load(bundle);
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
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
            lv_label_set_text(lbl, "No pedalboards found.");
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
            lv_obj_add_event_cb(btn, pedal_select_cb, LV_EVENT_CLICKED, g_pb_paths[i]);
        }
    } else {
        /* Pedalboards from selected bank */
        bank_t *bank = &g_banks.banks[g_selected_bank];
        if (bank->pedal_count == 0) {
            lv_obj_t *lbl = lv_label_create(g_list);
            lv_label_set_text(lbl, "No pedalboards in this bank.");
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
            return;
        }
        for (int i = 0; i < bank->pedal_count; i++) {
            lv_obj_t *btn = lv_list_add_button(g_list, LV_SYMBOL_AUDIO,
                                               bank->pedals[i].title);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
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
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, "Pedalboards");
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
        lv_label_set_text(lbl_all, "All");
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
