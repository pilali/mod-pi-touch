#include "ui_plugin_browser.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../plugin_manager.h"
#include "../host_comm.h"
#include "../pedalboard.h"

#include <string.h>
#include <stdio.h>

static lv_obj_t *g_parent      = NULL;
static lv_obj_t *g_search_ta   = NULL;
static lv_obj_t *g_list        = NULL;
static char      g_search[128] = "";

static void plugin_select_cb(lv_event_t *e);

static void refresh_list(void)
{
    if (!g_list) return;
    lv_obj_clean(g_list);

    int indices[512];
    int count;

    if (g_search[0]) {
        count = pm_search(g_search, indices, 512);
    } else {
        count = pm_plugin_count();
        for (int i = 0; i < count && i < 512; i++) indices[i] = i;
    }

    for (int i = 0; i < count; i++) {
        const pm_plugin_info_t *p = pm_plugin_at(indices[i]);
        if (!p) continue;

        lv_obj_t *btn = lv_list_add_button(g_list, NULL, p->name);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), UI_COLOR_TEXT, 0);

        /* Store URI as user data for click */
        lv_obj_set_user_data(btn, (void *)p->uri);
        lv_obj_add_event_cb(btn, plugin_select_cb, LV_EVENT_CLICKED, (void *)p->uri);
    }
}

static void plugin_select_cb(lv_event_t *e)
{
    const char *uri = lv_event_get_user_data(e);
    if (!uri) return;

    /* Find a free instance ID */
    extern pedalboard_t *ui_pedalboard_get(void);
    pedalboard_t *pb = ui_pedalboard_get();
    int instance_id = pb->plugin_count; /* Simple counter */

    /* Derive a symbol from URI */
    const char *last = strrchr(uri, '/');
    char sym[64];
    snprintf(sym, sizeof(sym), "plugin_%d", instance_id);
    if (last) snprintf(sym, sizeof(sym), "%s_%d", last + 1, instance_id);

    if (host_add_plugin(instance_id, uri) >= 0) {
        pb_plugin_t *plug = pb_add_plugin(pb, instance_id, sym, uri);
        if (plug) {
            /* Default position: center of visible area */
            plug->canvas_x = 200.0f + instance_id * 180.0f;
            plug->canvas_y = 200.0f;
            const pm_plugin_info_t *info = pm_plugin_by_uri(uri);
            if (info) snprintf(plug->label, sizeof(plug->label), "%s", info->name);
        }
        ui_app_show_screen(UI_SCREEN_PEDALBOARD);
        ui_pedalboard_refresh();
    } else {
        ui_app_show_message("Error", "Failed to add plugin.", 3000);
    }
}

static void search_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    snprintf(g_search, sizeof(g_search), "%s", lv_textarea_get_text(ta));
    refresh_list();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

void ui_plugin_browser_show(lv_obj_t *parent)
{
    g_parent = parent;
    g_search[0] = '\0';

    /* Layout */
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_row(parent, 8, 0); lv_obj_set_style_pad_column(parent, 8, 0);

    /* Header row */
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
    lv_label_set_text(hdr_lbl, "Add Plugin");
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);

    /* Search box */
    g_search_ta = lv_textarea_create(parent);
    lv_obj_set_size(g_search_ta, LV_PCT(100), 40);
    lv_textarea_set_placeholder_text(g_search_ta, "Search plugins...");
    lv_textarea_set_one_line(g_search_ta, true);
    lv_obj_add_event_cb(g_search_ta, search_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Plugin list */
    g_list = lv_list_create(parent);
    lv_obj_set_size(g_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(g_list, 1);
    lv_obj_set_style_bg_color(g_list, UI_COLOR_BG, 0);

    refresh_list();
}
