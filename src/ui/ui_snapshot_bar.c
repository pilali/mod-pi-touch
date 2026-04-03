#include "ui_snapshot_bar.h"
#include "ui_app.h"
#include "../pedalboard.h"

#include <string.h>
#include <stdio.h>

static lv_obj_t *g_bar     = NULL;
static lv_obj_t *g_btn_row = NULL;

extern pedalboard_t *ui_pedalboard_get(void);
extern bool          ui_pedalboard_is_loaded(void);

/* ─── Snapshot button callbacks ─────────────────────────────────────────────── */

static void snap_load_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    pb_snapshot_load(pb, idx);

    /* Apply to mod-host: send param_set for each snapshotable port */
    extern int host_param_set(int, const char *, float);
    extern int host_bypass(int, bool);
    pb_snapshot_t *snap = &pb->snapshots[idx];
    for (int i = 0; i < snap->plugin_count; i++) {
        snap_plugin_t *sp = &snap->plugins[i];
        pb_plugin_t *plug = pb_find_plugin_by_symbol(pb, sp->symbol);
        if (!plug) continue;
        host_bypass(plug->instance_id, sp->bypassed);
        for (int j = 0; j < sp->param_count; j++)
            host_param_set(plug->instance_id, sp->params[j].symbol, sp->params[j].value);
    }

    ui_snapshot_bar_refresh();
}

static void snap_name_entered_cb(const char *name, void *ud)
{
    (void)ud;
    pedalboard_t *pb2 = ui_pedalboard_get();
    if (pb2 && name && name[0])
        pb_snapshot_save_current(pb2, name);
    ui_snapshot_bar_refresh();
}

static void snap_add_cb(lv_event_t *e)
{
    (void)e;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    ui_app_show_input("Snapshot name", "e.g. Clean, Chorus...",
                      snap_name_entered_cb, NULL);
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_snapshot_bar_init(lv_obj_t *parent)
{
    g_bar = parent;
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_row(parent, 6, 0); lv_obj_set_style_pad_column(parent, 6, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Placeholder label */
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Snapshots:");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);

    g_btn_row = lv_obj_create(parent);
    lv_obj_set_height(g_btn_row, LV_PCT(100));
    lv_obj_set_flex_grow(g_btn_row, 1);
    lv_obj_set_flex_flow(g_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(g_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_btn_row, 0, 0);
    lv_obj_set_style_pad_all(g_btn_row, 0, 0);
    lv_obj_set_style_pad_row(g_btn_row, 6, 0); lv_obj_set_style_pad_column(g_btn_row, 6, 0);
    lv_obj_add_flag(g_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_btn_row, LV_DIR_HOR);

    /* Add snapshot button */
    lv_obj_t *btn_add = lv_btn_create(parent);
    lv_obj_set_size(btn_add, 36, 36);
    lv_obj_set_style_bg_color(btn_add, UI_COLOR_ACCENT, 0);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS);
    lv_obj_center(lbl_add);
    lv_obj_add_event_cb(btn_add, snap_add_cb, LV_EVENT_CLICKED, NULL);

    ui_snapshot_bar_refresh();
}

void ui_snapshot_bar_refresh(void)
{
    if (!g_btn_row) return;
    lv_obj_clean(g_btn_row);

    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded()) return;

    for (int i = 0; i < pb->snapshot_count; i++) {
        lv_obj_t *btn = lv_btn_create(g_btn_row);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, 44);
        lv_obj_set_style_pad_hor(btn, 12, 0);
        bool is_current = (i == pb->current_snapshot);
        lv_obj_set_style_bg_color(btn,
            is_current ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, pb->snapshots[i].name);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, snap_load_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}
