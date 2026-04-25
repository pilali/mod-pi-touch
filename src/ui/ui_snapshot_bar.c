#include "ui_snapshot_bar.h"
#include "ui_app.h"
#include "../pedalboard.h"
#include "../snapshot.h"
#include "../i18n.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern pedalboard_t *ui_pedalboard_get(void);
extern bool          ui_pedalboard_is_loaded(void);
extern void          ui_pedalboard_save_last_state(void);
extern int           host_param_set(int, const char *, float);
extern int           host_bypass(int, bool);
extern int           host_patch_set(int, const char *, const char *);

/* ─── State ──────────────────────────────────────────────────────────────────── */
static lv_obj_t *g_bar        = NULL;
static lv_obj_t *g_cur_label  = NULL;   /* "[snapshot name]" tap area */
static lv_obj_t *g_popup      = NULL;   /* snapshot list popup overlay */
static lv_obj_t *g_prefix_lbl = NULL;   /* "Snapshot:" static label */

/* ─── Forward declarations ───────────────────────────────────────────────────── */
static void close_popup(void);
static void close_popup_async(void);
static void open_popup(void);

/* ─── Apply snapshot to mod-host ─────────────────────────────────────────────── */
static void apply_snapshot(int idx)
{
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || idx < 0 || idx >= pb->snapshot_count) return;

    pb_snapshot_load(pb, idx);

    pb_snapshot_t *snap = &pb->snapshots[idx];
    for (int i = 0; i < snap->plugin_count; i++) {
        snap_plugin_t *sp = &snap->plugins[i];
        pb_plugin_t *plug = pb_find_plugin_by_symbol(pb, sp->symbol);
        if (!plug) continue;
        host_bypass(plug->instance_id, sp->bypassed);
        for (int j = 0; j < sp->param_count; j++)
            host_param_set(plug->instance_id, sp->params[j].symbol, sp->params[j].value);
        for (int j = 0; j < sp->patch_param_count; j++)
            if (sp->patch_params[j].path[0])
                host_patch_set(plug->instance_id,
                               sp->patch_params[j].uri,
                               sp->patch_params[j].path);
    }
    ui_snapshot_bar_refresh();
    ui_pedalboard_save_last_state();
}

/* ─── Context menu (long press) ──────────────────────────────────────────────── */

typedef struct { int idx; lv_obj_t *overlay; } ctx_menu_t;

static void ctx_rename_input_cb(const char *name, void *ud)
{
    int idx = (int)(intptr_t)ud;
    pedalboard_t *pb = ui_pedalboard_get();
    if (pb && name && name[0])
        pb_snapshot_rename(pb, idx, name);
    ui_snapshot_bar_refresh();
}

static void ctx_rename_cb(lv_event_t *e)
{
    ctx_menu_t *m = lv_event_get_user_data(e);
    int idx = m->idx;
    lv_obj_add_flag(m->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(m->overlay);
    free(m);
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    ui_app_show_input(TR(TR_SNAP_RENAME),
                      pb->snapshots[idx].name,
                      ctx_rename_input_cb, (void *)(intptr_t)idx);
}

static void ctx_delete_confirmed(void *ud)
{
    int idx = (int)(intptr_t)ud;
    pedalboard_t *pb = ui_pedalboard_get();
    if (pb) pb_snapshot_delete(pb, idx);
    ui_snapshot_bar_refresh();
}

static void ctx_delete_cb(lv_event_t *e)
{
    ctx_menu_t *m = lv_event_get_user_data(e);
    int idx = m->idx;
    lv_obj_add_flag(m->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(m->overlay);
    free(m);
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    char msg[256];
    snprintf(msg, sizeof(msg), TR(TR_CONFIRM_DELETE_SNAP), pb->snapshots[idx].name);
    ui_app_show_confirm(TR(TR_SNAP_DELETE_TITLE), msg,
                        ctx_delete_confirmed, (void *)(intptr_t)idx);
}

static void ctx_cancel_cb(lv_event_t *e)
{
    ctx_menu_t *m = lv_event_get_user_data(e);
    lv_obj_add_flag(m->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(m->overlay);
    free(m);
}

static void show_context_menu(int idx)
{
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 300, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);

    char title[PB_NAME_MAX + 16];
    snprintf(title, sizeof(title), "\"%s\"", pb->snapshots[idx].name);
    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    ctx_menu_t *m_rename = malloc(sizeof(*m_rename));
    m_rename->idx = idx; m_rename->overlay = overlay;
    lv_obj_t *btn_rename = lv_btn_create(panel);
    lv_obj_set_size(btn_rename, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(btn_rename, UI_COLOR_ACCENT, 0);
    lv_obj_t *l_r = lv_label_create(btn_rename);
    lv_label_set_text_fmt(l_r, LV_SYMBOL_EDIT "%s", TR(TR_SNAP_RENAME));
    lv_obj_center(l_r);
    lv_obj_set_style_text_color(l_r, UI_COLOR_TEXT, 0);
    lv_obj_add_event_cb(btn_rename, ctx_rename_cb, LV_EVENT_CLICKED, m_rename);

    ctx_menu_t *m_del = malloc(sizeof(*m_del));
    m_del->idx = idx; m_del->overlay = overlay;
    lv_obj_t *btn_del = lv_btn_create(panel);
    lv_obj_set_size(btn_del, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(btn_del, UI_COLOR_BYPASS, 0);
    lv_obj_t *l_d = lv_label_create(btn_del);
    lv_label_set_text_fmt(l_d, LV_SYMBOL_TRASH "%s", TR(TR_SNAP_DELETE_BTN));
    lv_obj_center(l_d);
    lv_obj_set_style_text_color(l_d, UI_COLOR_TEXT, 0);
    lv_obj_add_event_cb(btn_del, ctx_delete_cb, LV_EVENT_CLICKED, m_del);

    ctx_menu_t *m_can = malloc(sizeof(*m_can));
    m_can->idx = idx; m_can->overlay = overlay;
    lv_obj_t *btn_can = lv_btn_create(panel);
    lv_obj_set_size(btn_can, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(btn_can, UI_COLOR_SURFACE, 0);
    lv_obj_t *l_c = lv_label_create(btn_can);
    lv_label_set_text(l_c, TR(TR_CANCEL));
    lv_obj_center(l_c);
    lv_obj_set_style_text_color(l_c, UI_COLOR_TEXT_DIM, 0);
    lv_obj_add_event_cb(btn_can, ctx_cancel_cb, LV_EVENT_CLICKED, m_can);
}

/* ─── Snapshot list popup ────────────────────────────────────────────────────── */

static void popup_close_cb(lv_event_t *e)
{
    (void)e;
    close_popup_async();
}

typedef struct { int idx; } snap_item_ud_t;
static snap_item_ud_t g_item_uds[PB_MAX_SNAPSHOTS];

static void snap_item_click_cb(lv_event_t *e)
{
    snap_item_ud_t *ud = lv_event_get_user_data(e);
    int idx = ud->idx;
    close_popup_async();
    apply_snapshot(idx);
}

static void snap_item_longpress_cb(lv_event_t *e)
{
    snap_item_ud_t *ud = lv_event_get_user_data(e);
    int idx = ud->idx;
    close_popup_async();
    show_context_menu(idx);
}

static void open_popup(void)
{
    if (g_popup) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded() || pb->snapshot_count == 0) return;

    /* Full-screen overlay to catch outside taps */
    g_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_popup, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_popup, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_popup, LV_OPA_60, 0);
    lv_obj_add_event_cb(g_popup, popup_close_cb, LV_EVENT_CLICKED, NULL);

    /* List panel — positioned above snapshot bar */
    int item_h = 52;
    int max_visible = 7;
    int visible = pb->snapshot_count < max_visible ? pb->snapshot_count : max_visible;
    int panel_h = visible * item_h + 16;

    lv_obj_t *panel = lv_obj_create(g_popup);
    lv_obj_set_size(panel, LV_PCT(90), panel_h);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -(UI_SNAPSHOT_BAR_H + 4));
    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_40, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(panel, 32, 0);
    lv_obj_set_style_shadow_spread(panel, 4, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < pb->snapshot_count; i++) {
        g_item_uds[i].idx = i;
        bool is_cur = (i == pb->current_snapshot);

        lv_obj_t *btn = lv_btn_create(panel);
        lv_obj_set_size(btn, LV_PCT(100), item_h - 4);
        lv_obj_set_style_bg_color(btn,
            is_cur ? UI_COLOR_PRIMARY : UI_COLOR_BG, 0);
        lv_obj_set_style_radius(btn, 4, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, pb->snapshots[i].name);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_add_event_cb(btn, snap_item_click_cb,     LV_EVENT_CLICKED,       &g_item_uds[i]);
        lv_obj_add_event_cb(btn, snap_item_longpress_cb, LV_EVENT_LONG_PRESSED,  &g_item_uds[i]);
    }
}

/* Sync — safe only from external callers (refresh, screen transitions). */
static void close_popup(void)
{
    if (g_popup) { lv_obj_del(g_popup); g_popup = NULL; }
}

/* Async — required when called from within a g_popup child event handler. */
static void close_popup_async(void)
{
    if (g_popup) {
        lv_obj_add_flag(g_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(g_popup);
        g_popup = NULL;
    }
}

/* ─── Add snapshot ───────────────────────────────────────────────────────────── */

static void snap_name_entered_cb(const char *name, void *ud)
{
    (void)ud;
    pedalboard_t *pb = ui_pedalboard_get();
    if (pb && name && name[0])
        pb_snapshot_save_current(pb, name);
    ui_snapshot_bar_refresh();
}

static void snap_add_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    ui_app_show_input(TR(TR_SNAP_NEW_TITLE), TR(TR_SNAP_NEW_HINT),
                      snap_name_entered_cb, NULL);
}

/* ─── Current snapshot tap ───────────────────────────────────────────────────── */

static void snap_label_tap_cb(lv_event_t *e)
{
    (void)e;
    if (g_popup) close_popup();
    else         open_popup();
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_snapshot_bar_init(lv_obj_t *parent)
{
    g_bar = parent;
    g_popup = NULL;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_column(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* "Snapshot:" static label */
    g_prefix_lbl = lv_label_create(parent);
    lv_label_set_text(g_prefix_lbl, TR(TR_SNAP_LABEL));
    lv_obj_set_style_text_color(g_prefix_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(g_prefix_lbl, &lv_font_montserrat_14, 0);

    /* Current snapshot name — tappable button area */
    lv_obj_t *tap_btn = lv_btn_create(parent);
    lv_obj_set_flex_grow(tap_btn, 1);
    lv_obj_set_height(tap_btn, 44);
    lv_obj_set_style_bg_color(tap_btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(tap_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(tap_btn, 1, 0);
    lv_obj_set_style_border_opa(tap_btn, LV_OPA_40, 0);
    lv_obj_set_style_radius(tap_btn, 22, 0);
    lv_obj_set_style_shadow_width(tap_btn, 0, 0);
    lv_obj_set_style_pad_hor(tap_btn, 10, 0);

    g_cur_label = lv_label_create(tap_btn);
    lv_label_set_text(g_cur_label, "—");
    lv_obj_set_style_text_color(g_cur_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(g_cur_label, &lv_font_montserrat_16, 0);
    lv_obj_align(g_cur_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_long_mode(g_cur_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_cur_label, LV_PCT(85));

    /* Arrow indicator */
    lv_obj_t *arrow = lv_label_create(tap_btn);
    lv_label_set_text(arrow, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(arrow, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(tap_btn, snap_label_tap_cb, LV_EVENT_CLICKED, NULL);

    /* + button */
    lv_obj_t *btn_add = lv_btn_create(parent);
    lv_obj_set_size(btn_add, 44, 44);
    lv_obj_set_style_bg_color(btn_add, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_add, 22, 0);
    lv_obj_set_style_shadow_color(btn_add, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_width(btn_add, 14, 0);
    lv_obj_set_style_shadow_spread(btn_add, 2, 0);
    lv_obj_set_style_shadow_opa(btn_add, LV_OPA_40, 0);
    lv_obj_set_style_shadow_ofs_x(btn_add, 0, 0);
    lv_obj_set_style_shadow_ofs_y(btn_add, 0, 0);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS);
    lv_obj_center(lbl_add);
    lv_obj_add_event_cb(btn_add, snap_add_cb, LV_EVENT_CLICKED, NULL);

    ui_snapshot_bar_refresh();
}

void ui_snapshot_bar_dismiss(void)
{
    if (g_popup) {
        lv_obj_delete_async(g_popup);
        g_popup = NULL;
    }
}

void ui_snapshot_bar_update_lang(void)
{
    if (g_prefix_lbl)
        lv_label_set_text(g_prefix_lbl, TR(TR_SNAP_LABEL));
}

void ui_snapshot_bar_refresh(void)
{
    if (!g_cur_label) return;

    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded()) {
        lv_label_set_text(g_cur_label, "—");
        return;
    }

    int idx = pb->current_snapshot;
    if (idx >= 0 && idx < pb->snapshot_count)
        lv_label_set_text(g_cur_label, pb->snapshots[idx].name);
    else
        lv_label_set_text(g_cur_label, "—");
}
