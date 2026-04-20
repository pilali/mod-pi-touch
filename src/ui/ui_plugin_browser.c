#include "ui_plugin_browser.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../plugin_manager.h"
#include "../host_comm.h"
#include "../pedalboard.h"
#include "../i18n.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/* ─── State ─────────────────────────────────────────────────────────────────── */

#define MAX_CATS 64

static lv_obj_t *g_scroll    = NULL;
static lv_obj_t *g_search_ta = NULL;
static lv_obj_t *g_keyboard  = NULL;
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

/* ─── Plugin add (async — host_add_plugin can block several seconds) ─────────── */

typedef struct {
    int  instance_id;
    char uri[PM_URI_MAX];
    char sym[64];
    int  result;
} add_plugin_ctx_t;

static void add_plugin_done(void *arg)
{
    add_plugin_ctx_t *ctx = arg;

    if (ctx->result >= 0) {
        extern pedalboard_t *ui_pedalboard_get(void);
        pedalboard_t *pb = ui_pedalboard_get();
        if (pb) {
            pb_plugin_t *plug = pb_add_plugin(pb, ctx->instance_id, ctx->sym, ctx->uri);
            if (plug) {
                plug->canvas_x = 200.0f + ctx->instance_id * 180.0f;
                plug->canvas_y = 200.0f;
                const pm_plugin_info_t *info = pm_plugin_by_uri(ctx->uri);
                if (info) {
                    snprintf(plug->label, sizeof(plug->label), "%s", info->name);
                    /* Populate ports from plugin manager so the param editor has
                     * something to show immediately (without a TTL round-trip). */
                    plug->port_count = 0;
                    for (int i = 0; i < info->port_count && plug->port_count < PB_MAX_PORTS; i++) {
                        const pm_port_info_t *pi = &info->ports[i];
                        if (pi->type != PM_PORT_CONTROL_IN) continue;
                        pb_port_t *pp = &plug->ports[plug->port_count++];
                        snprintf(pp->symbol, sizeof(pp->symbol), "%s", pi->symbol);
                        pp->value        = pi->default_val;
                        pp->min          = pi->min;
                        pp->max          = pi->max;
                        pp->snapshotable = true;
                        pp->midi_channel = -1;
                        pp->midi_cc      = -1;
                    }
                    /* Populate patch params (e.g. AIDA-X model file) */
                    plug->patch_param_count = 0;
                    for (int i = 0; i < info->patch_param_count && plug->patch_param_count < PB_MAX_PATCH_PARAMS; i++) {
                        pb_patch_t *pp = &plug->patch_params[plug->patch_param_count++];
                        snprintf(pp->uri,  sizeof(pp->uri),  "%s", info->patch_params[i].uri);
                        pp->path[0] = '\0';
                    }
                }
            }
        }
        ui_app_show_screen(UI_SCREEN_PEDALBOARD);
        ui_pedalboard_refresh();
    } else {
        const pm_plugin_info_t *info = pm_plugin_by_uri(ctx->uri);
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to load:\n%s",
                 info ? info->name : ctx->uri);
        ui_app_show_toast_error(msg);
    }
    free(ctx);
}

static void *add_plugin_thread(void *arg)
{
    add_plugin_ctx_t *ctx = arg;
    ctx->result = host_add_plugin(ctx->instance_id, ctx->uri);
    lv_async_call(add_plugin_done, ctx);
    return NULL;
}

static void plugin_select_cb(lv_event_t *e)
{
    const char *uri = lv_event_get_user_data(e);
    if (!uri) return;

    extern pedalboard_t *ui_pedalboard_get(void);
    pedalboard_t *pb = ui_pedalboard_get();

    int instance_id = 0;
    for (int i = 0; i < pb->plugin_count; i++)
        if (pb->plugins[i].instance_id >= instance_id)
            instance_id = pb->plugins[i].instance_id + 1;

    add_plugin_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) return;
    ctx->instance_id = instance_id;
    snprintf(ctx->uri, sizeof(ctx->uri), "%s", uri);
    const char *last = strrchr(uri, '/');
    snprintf(ctx->sym, sizeof(ctx->sym), "%s_%d", last ? last + 1 : "plugin", instance_id);

    ui_app_show_toast(TR(TR_PLUGIN_LOADING));

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, add_plugin_thread, ctx);
    pthread_attr_destroy(&attr);
}

/* ─── Tree refresh ───────────────────────────────────────────────────────────── */

static void cat_toggle_cb(lv_event_t *e); /* forward declaration */

static void rebuild_category_tree(void)
{
    if (!g_scroll) return;
    lv_obj_clean(g_scroll);

    for (int i = 0; i < g_cat_count; i++) {
        /* Get plugin count for this category */
        int indices_tmp[512];
        int n_plugins = pm_plugins_in_category(g_cats[i], indices_tmp, 512);
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

        /* Plugin rows — indented (reuse indices_tmp filled above) */
        for (int j = 0; j < n_plugins; j++) {
            const pm_plugin_info_t *p = pm_plugin_at(indices_tmp[j]);
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
    /* Save scroll position before toggle so we can restore after rebuild */
    lv_coord_t scroll_before = g_scroll ? lv_obj_get_scroll_y(g_scroll) : 0;

    int ci = (int)(intptr_t)lv_event_get_user_data(e);
    if (ci >= 0 && ci < g_cat_count)
        g_expanded[ci] = !g_expanded[ci];
    rebuild_category_tree();

    /* Restore scroll — use a small async so LVGL has laid out new content */
    if (g_scroll)
        lv_obj_scroll_to_y(g_scroll, scroll_before, LV_ANIM_OFF);
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
            lv_label_set_text(lbl, TR(TR_PLUGIN_NOT_FOUND));
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

/* ─── Keyboard ───────────────────────────────────────────────────────────────── */

static void keyboard_ready_cb(lv_event_t *e)
{
    (void)e;
    /* "OK" / "Hide" pressed — unfocus textarea and hide keyboard */
    if (g_keyboard) lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (g_search_ta) lv_obj_clear_state(g_search_ta, LV_STATE_FOCUSED);
}

static void ta_focused_cb(lv_event_t *e)
{
    (void)e;
    if (!g_keyboard) return;
    lv_keyboard_set_textarea(g_keyboard, g_search_ta);
    lv_obj_clear_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void ta_defocused_cb(lv_event_t *e)
{
    (void)e;
    if (g_keyboard) lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    /* Hide keyboard before leaving */
    if (g_keyboard) {
        lv_obj_del(g_keyboard);
        g_keyboard = NULL;
    }
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

/* ─── Public ─────────────────────────────────────────────────────────────────── */

void ui_plugin_browser_show(lv_obj_t *parent)
{
    g_search[0] = '\0';
    g_scroll    = NULL;
    g_search_ta = NULL;
    g_keyboard  = NULL;

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
    lv_label_set_text_fmt(lbl_back, LV_SYMBOL_LEFT " %s", TR(TR_BACK));
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, TR(TR_PLUGIN_BROWSER_TITLE));
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);

    /* ── Search box ── */
    g_search_ta = lv_textarea_create(parent);
    lv_obj_set_size(g_search_ta, LV_PCT(100), 40);
    lv_textarea_set_placeholder_text(g_search_ta, TR(TR_PLUGIN_SEARCH_HINT));
    lv_textarea_set_one_line(g_search_ta, true);
    lv_obj_add_event_cb(g_search_ta, search_cb,      LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_search_ta, ta_focused_cb,  LV_EVENT_FOCUSED,       NULL);
    lv_obj_add_event_cb(g_search_ta, ta_defocused_cb,LV_EVENT_DEFOCUSED,     NULL);

    /* ── Virtual keyboard (hidden until textarea focused) ── */
    g_keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(g_keyboard, LV_PCT(100), LV_VER_RES / 3);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    ui_app_keyboard_apply_lang(g_keyboard);
    lv_obj_add_event_cb(g_keyboard, keyboard_ready_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(g_keyboard, keyboard_ready_cb, LV_EVENT_CANCEL, NULL);

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
