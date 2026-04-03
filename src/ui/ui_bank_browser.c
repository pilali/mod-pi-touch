#include "ui_bank_browser.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../bank.h"
#include "../settings.h"
#include "../pedalboard.h"

#include <string.h>
#include <stdio.h>

static bank_list_t g_banks;
static lv_obj_t   *g_list = NULL;

static void pedal_select_cb(lv_event_t *e)
{
    const char *bundle = lv_event_get_user_data(e);
    if (!bundle) return;
    ui_pedalboard_load(bundle);
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

void ui_bank_browser_show(lv_obj_t *parent)
{
    /* Load banks */
    mpt_settings_t *s = settings_get();
    bank_load(s->banks_file, &g_banks);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_row(parent, 8, 0); lv_obj_set_style_pad_column(parent, 8, 0);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 44);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_pad_row(hdr, 8, 0); lv_obj_set_style_pad_column(hdr, 8, 0);

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

    /* List */
    g_list = lv_list_create(parent);
    lv_obj_set_size(g_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(g_list, 1);
    lv_obj_set_style_bg_color(g_list, UI_COLOR_BG, 0);

    /* Also scan user pedalboards directory */
    char *pb_paths[256];
    int np = pb_list(s->pedalboards_dir, pb_paths, 256);

    /* Pedalboards from filesystem */
    if (np > 0) {
        lv_list_add_text(g_list, "My Pedalboards");
        for (int i = 0; i < np; i++) {
            /* Extract name from path */
            const char *sl = strrchr(pb_paths[i], '/');
            char name[256];
            snprintf(name, sizeof(name), "%s", sl ? sl + 1 : pb_paths[i]);
            char *dot = strrchr(name, '.');
            if (dot) *dot = '\0';

            lv_obj_t *btn = lv_list_add_button(g_list, LV_SYMBOL_AUDIO, name);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_set_user_data(btn, pb_paths[i]);
            lv_obj_add_event_cb(btn, pedal_select_cb, LV_EVENT_CLICKED, pb_paths[i]);
        }
    }

    /* Factory pedalboards */
    char *fact_paths[256];
    int nf = pb_list(s->factory_pedalboards_dir, fact_paths, 256);
    if (nf > 0) {
        lv_list_add_text(g_list, "Factory Pedalboards");
        for (int i = 0; i < nf; i++) {
            const char *sl = strrchr(fact_paths[i], '/');
            char name[256];
            snprintf(name, sizeof(name), "%s", sl ? sl + 1 : fact_paths[i]);
            char *dot = strrchr(name, '.');
            if (dot) *dot = '\0';

            lv_obj_t *btn = lv_list_add_button(g_list, LV_SYMBOL_AUDIO, name);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_add_event_cb(btn, pedal_select_cb, LV_EVENT_CLICKED, fact_paths[i]);
        }
    }

    if (np == 0 && nf == 0) {
        lv_obj_t *lbl = lv_label_create(g_list);
        lv_label_set_text(lbl, "No pedalboards found.");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    }
}
