#include "ui_plugin_browser.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../plugin_manager.h"
#include "../host_comm.h"
#include "../pedalboard.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── State ─────────────────────────────────────────────────────────────────── */

#define MAX_CATS 64

static lv_obj_t *g_scroll    = NULL;
static lv_obj_t *g_search_ta = NULL;
static char      g_search[128] = "";

static char g_cats[MAX_CATS][PM_CAT_MAX];
static int  g_cat_count = 0;
static bool g_expanded[MAX_CATS];

/* ─── Helpers ────────────────────────────────────────────────────────────────── */

static int cat_strcmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void load_categories(void)
{
    g_cat_count = pm_categories(g_cats, MAX_CATS);
    qsort(g_cats, g_cat_count, PM_CAT_MAX, cat_strcmp);
    /* All categories start collapsed */
    memset(g_expanded, 0, sizeof(g_expanded));
}

/* ─── Plugin add ─────────────────────────────────────────────────────────────── */

static void plugin_select_cb(lv_event_t *e)
{
    const char *uri = lv_event_get_user_data(e);
    if (!uri) return;

    extern pedalboard_t *ui_pedalboard_get(void);
    pedalboard_t *pb = ui_pedalboard_get();
    int instance_id = pb->plugin_count;

    char sym[64];
    const char *last = strrchr(uri, '/');
    snprintf(sym, sizeof(sym), "%s_%d", last ? last + 1 : "plugin", instance_id);

    if (host_add_plugin(instance_id, uri) >= 0) {
        pb_plugin_t *plug = pb_add_plugin(pb, instance_id, sym, uri);
        if (plug) {
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

/* ─── Tree refresh ───────────────────────────────────────────────────────────── */

static void cat_toggle_cb(lv_event_t *e); /* forward declaration */

static void rebuild_category_tree(void)
{
    if (!g_scroll) return;
    lv_obj_clean(g_scroll);

    for (int i = 0; i < g_cat_count; i++) {
        /* Count plugins in this category */
        int n_plugins = pm_plugins_in_category(g_cats[i], NULL, 0);
        if (n_plugins == 0) continue;

        /* Category header button */
        char hdr[PM_CAT_MAX + 8];
        snprintf(hdr, sizeof(hdr), "%s %s (%d)",
                 g_expanded[i] ? "-" : "+", g_cats[i], n_plugins);

        lv_obj_t *cat_btn = lv_btn_create(g_scroll);
        lv_obj_set_size(cat_btn, LV_PCT(100), 40);
        lv_obj_set_style_radius(cat_btn, 4, 0);
        lv_obj_set_style_bg_color(cat_btn,
            g_expanded[i] ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0);
        lv_obj_t *cat_lbl = lv_label_create(cat_btn);
        lv_label_set_text(cat_lbl, hdr);
        lv_obj_align(cat_lbl, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_style_text_color(cat_lbl, UI_COLOR_TEXT, 0);
        lv_obj_add_event_cb(cat_btn, cat_toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (!g_expanded[i]) continue;

        /* Plugin rows — indented */
        int indices[512];
        int count = pm_plugins_in_category(g_cats[i], indices, 512);
        for (int j = 0; j < count; j++) {
            const pm_plugin_info_t *p = pm_plugin_at(indices[j]);
            if (!p) continue;

            lv_obj_t *row = lv_obj_create(g_scroll);
            lv_obj_set_size(row, LV_PCT(100), 36);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);

            /* Indented plugin button */
            lv_obj_t *btn = lv_btn_create(row);
            lv_obj_set_pos(btn, 20, 0);
            lv_obj_set_size(btn, lv_pct(100) - 20, 34);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_set_style_radius(btn, 4, 0);

            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, p->name);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);

            lv_obj_add_event_cb(btn, plugin_select_cb, LV_EVENT_CLICKED, (void *)p->uri);
        }
    }
}

static void cat_toggle_cb(lv_event_t *e)
{
    int ci = (int)(intptr_t)lv_event_get_user_data(e);
    if (ci >= 0 && ci < g_cat_count)
        g_expanded[ci] = !g_expanded[ci];
    rebuild_category_tree();
}

static void refresh_tree(void)
{
    if (!g_scroll) return;
    lv_obj_clean(g_scroll);

    if (g_search[0]) {
        /* Flat search results */
        int indices[512];
        int count = pm_search(g_search, indices, 512);
        if (count == 0) {
            lv_obj_t *lbl = lv_label_create(g_scroll);
            lv_label_set_text(lbl, "No plugins found.");
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
            return;
        }
        for (int i = 0; i < count; i++) {
            const pm_plugin_info_t *p = pm_plugin_at(indices[i]);
            if (!p) continue;
            lv_obj_t *btn = lv_btn_create(g_scroll);
            lv_obj_set_size(btn, LV_PCT(100), 36);
            lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, p->name);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
            lv_obj_add_event_cb(btn, plugin_select_cb, LV_EVENT_CLICKED, (void *)p->uri);
        }
        return;
    }

    rebuild_category_tree();
}

/* ─── Callbacks ──────────────────────────────────────────────────────────────── */

static void search_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    snprintf(g_search, sizeof(g_search), "%s", lv_textarea_get_text(ta));
    refresh_tree();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

/* ─── Public ─────────────────────────────────────────────────────────────────── */

void ui_plugin_browser_show(lv_obj_t *parent)
{
    g_search[0] = '\0';
    g_scroll = NULL;
    g_search_ta = NULL;

    if (g_cat_count == 0) load_categories();

    /* Layout */
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
    lv_label_set_text(hdr_lbl, "Add Plugin");
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);

    /* ── Search box ── */
    g_search_ta = lv_textarea_create(parent);
    lv_obj_set_size(g_search_ta, LV_PCT(100), 40);
    lv_textarea_set_placeholder_text(g_search_ta, "Search plugins...");
    lv_textarea_set_one_line(g_search_ta, true);
    lv_obj_add_event_cb(g_search_ta, search_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Scrollable category tree ── */
    g_scroll = lv_obj_create(parent);
    lv_obj_set_size(g_scroll, LV_PCT(100), 0);
    lv_obj_set_flex_grow(g_scroll, 1);
    lv_obj_set_flex_flow(g_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(g_scroll, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(g_scroll, 0, 0);
    lv_obj_set_style_pad_all(g_scroll, 4, 0);
    lv_obj_set_style_pad_row(g_scroll, 3, 0);

    refresh_tree();
}
