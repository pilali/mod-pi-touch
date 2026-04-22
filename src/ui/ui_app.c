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
#include "ui_scene.h"

#include "../bank.h"
#include "../snapshot.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── AZERTY keyboard maps (French) ─────────────────────────────────────────
 * Same row structure as the default QWERTY maps so the ctrl maps are reused.
 * Changes vs QWERTY: row-1 a/z instead of q/w; row-2 q instead of a. */
#define KB_BTN(w) (LV_BUTTONMATRIX_CTRL_POPOVER | (w))

/* Row 3: common French accented chars replace the special chars of QWERTY.
 * Lower: é è | w x c v b n m | à ç ù
 * Upper: É È | W X C V B N M | À Ç Ù  */
static const char * const azerty_map_lc[] = {
    "1#", "a", "z", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "q", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "\xc3\xa9", "\xc3\xa8", "w", "x", "c", "v", "b", "n", "m", "\xc3\xa0", "\xc3\xa7", "\xc3\xb9", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const char * const azerty_map_uc[] = {
    "1#", "A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "Q", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "\xc3\x89", "\xc3\x88", "W", "X", "C", "V", "B", "N", "M", "\xc3\x80", "\xc3\x87", "\xc3\x99", "\n",
    LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
/* ctrl maps: row 3 uses plain KB_BTN(1) for all keys (no "special" styling) */
static const lv_buttonmatrix_ctrl_t azerty_ctrl_lc[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};
static const lv_buttonmatrix_ctrl_t azerty_ctrl_uc[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), KB_BTN(3), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

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
static lv_obj_t   *g_prefx_label  = NULL;
static lv_obj_t   *g_cond_label   = NULL;
static lv_obj_t   *g_scene_label  = NULL;
static lv_obj_t   *g_set_label    = NULL;
static lv_obj_t   *g_save_label   = NULL;
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
    if (g_current_screen == UI_SCREEN_BANK_BROWSER)
        ui_app_show_screen(UI_SCREEN_PEDALBOARD);
    else
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

static void btn_scene_cb(lv_event_t *e)
{
    (void)e;
    if (ui_scene_is_open()) ui_scene_close();
    else                    ui_scene_open();
}

static void btn_add_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PLUGIN_BROWSER);
}

/* ─── Save menu ──────────────────────────────────────────────────────────────── */

static lv_obj_t *g_save_menu = NULL;

/* Sync close */
static void save_menu_close(void)
{
    if (g_save_menu) { lv_obj_del(g_save_menu); g_save_menu = NULL; }
}

/* Async close — required when called from within the menu's event tree */
static void save_menu_close_async(void)
{
    if (g_save_menu) {
        lv_obj_add_flag(g_save_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(g_save_menu);
        g_save_menu = NULL;
    }
}

static void save_menu_overlay_cb(lv_event_t *e) { (void)e; save_menu_close_async(); }

/* Replace characters that are not alphanumeric or '-' with '_', so the
 * result is safe to use as a filesystem path component and a Turtle URI. */
static void name_to_slug(const char *name, char *slug, size_t sz)
{
    size_t i = 0;
    for (const char *p = name; *p && i < sz - 1; p++, i++) {
        unsigned char c = (unsigned char)*p;
        slug[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-') ? (char)c : '_';
    }
    slug[i] = '\0';
}

/* ── Save / new pedalboard ── */

static void do_save_pedalboard(void *ud)
{
    (void)ud;
    ui_pedalboard_save();
    ui_app_show_toast(TR(TR_MSG_PB_SAVED));
}
static void confirm_save_pedalboard_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    ui_app_show_confirm(TR(TR_MENU_SAVE_PB), TR(TR_CONFIRM_SAVE_PB),
                        do_save_pedalboard, NULL);
}

static void do_new_pedalboard(const char *name, void *ud)
{
    (void)ud;
    if (!name || !name[0]) return;

    mpt_settings_t *s = settings_get();
    char slug[PB_NAME_MAX];
    name_to_slug(name, slug, sizeof(slug));
    char new_dir[PB_PATH_MAX];
    snprintf(new_dir, sizeof(new_dir), "%s/%s.pedalboard", s->pedalboards_dir, slug);

    pedalboard_t *new_pb = malloc(sizeof(*new_pb));
    if (!new_pb) { ui_app_show_toast_error(TR(TR_MSG_PB_SAVE_ERROR)); return; }
    pb_init(new_pb);
    snprintf(new_pb->name, sizeof(new_pb->name), "%s", name);
    snprintf(new_pb->path, sizeof(new_pb->path), "%s", new_dir);

    pb_plugin_t *vol = pb_add_plugin(new_pb, 0, "volume",
                                     "http://moddevices.com/plugins/mod-devel/mod-volume-2x2");
    if (vol) {
        vol->canvas_x = 540.0f;
        vol->canvas_y = 100.0f;
        char base[PB_PATH_MAX + 8];
        snprintf(base, sizeof(base), "file://%s", new_dir);
        char from[PB_URI_MAX], to[PB_URI_MAX];
        snprintf(from, sizeof(from), "%s/capture_1",        base);
        snprintf(to,   sizeof(to),   "%s/volume/AudioIn1",  base);
        pb_add_connection(new_pb, from, to);
        snprintf(from, sizeof(from), "%s/capture_2",        base);
        snprintf(to,   sizeof(to),   "%s/volume/AudioIn2",  base);
        pb_add_connection(new_pb, from, to);
        snprintf(from, sizeof(from), "%s/volume/AudioOut1", base);
        snprintf(to,   sizeof(to),   "%s/playback_1",       base);
        pb_add_connection(new_pb, from, to);
        snprintf(from, sizeof(from), "%s/volume/AudioOut2", base);
        snprintf(to,   sizeof(to),   "%s/playback_2",       base);
        pb_add_connection(new_pb, from, to);
    }

    int r = pb_save(new_pb);
    free(new_pb);
    if (r < 0) { ui_app_show_toast_error(TR(TR_MSG_PB_SAVE_ERROR)); return; }
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
    ui_pedalboard_load(new_dir, NULL, NULL);
    ui_app_show_toast(TR(TR_MSG_PB_SAVED));
}
static void new_pb_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    ui_app_show_input(TR(TR_MENU_NEW_PB), "", do_new_pedalboard, NULL);
}

/* ── Save As pedalboard ── */

static void do_save_as_input(const char *name, void *ud)
{
    (void)ud;
    if (!name || !name[0]) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;

    mpt_settings_t *s = settings_get();
    char slug[PB_NAME_MAX];
    name_to_slug(name, slug, sizeof(slug));
    char new_dir[PB_PATH_MAX];
    snprintf(new_dir, sizeof(new_dir), "%s/%s.pedalboard", s->pedalboards_dir, slug);

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
    const char *cur = pb ? pb->name : "";
    ui_app_show_input(TR(TR_MENU_SAVE_PB_AS), cur, do_save_as_input, NULL);
}

/* ── Rename pedalboard ── */

static void do_rename_pedalboard(const char *name, void *ud)
{
    (void)ud;
    if (!name || !name[0]) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded()) return;

    mpt_settings_t *s = settings_get();
    char old_path[PB_PATH_MAX];
    snprintf(old_path, sizeof(old_path), "%s", pb->path);

    char slug[PB_NAME_MAX];
    name_to_slug(name, slug, sizeof(slug));
    char new_path[PB_PATH_MAX];
    snprintf(new_path, sizeof(new_path), "%s/%s.pedalboard", s->pedalboards_dir, slug);

    if (rename(old_path, new_path) != 0) {
        ui_app_show_toast_error(TR(TR_MSG_PB_SAVE_ERROR));
        return;
    }

    snprintf(pb->path, sizeof(pb->path), "%s", new_path);
    snprintf(pb->name, sizeof(pb->name), "%s", name);
    pb_save(pb);

    /* Update banks.json */
    bank_list_t banks;
    if (bank_load(s->banks_file, &banks) == 0) {
        bank_update_bundle_path(&banks, old_path, new_path);
        bank_save(s->banks_file, &banks);
    }

    ui_app_update_title(name, false);
    ui_pedalboard_save_last_state();
    ui_app_show_toast(TR(TR_MSG_PB_SAVED));
}
static void rename_pb_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded()) {
        ui_app_show_message(TR(TR_MSG_NO_PB_LOADED_TITLE), TR(TR_MSG_NO_PB_LOADED), 2000);
        return;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), TR(TR_CONFIRM_RENAME_PB), pb->name);
    ui_app_show_input(msg, pb->name, do_rename_pedalboard, NULL);
}

/* ── Delete pedalboard ── */

static void do_delete_pedalboard(void *ud)
{
    (void)ud;
    ui_pedalboard_delete();
    ui_app_show_toast(TR(TR_MSG_PB_DELETED));
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

/* ── New snapshot ── */

static void do_new_snapshot(const char *name, void *ud)
{
    (void)ud;
    if (!name || !name[0]) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded()) return;

    if (pb_snapshot_save_current(pb, name) < 0) {
        ui_app_show_toast_error(TR(TR_MSG_SNAP_SAVE_FAIL));
        return;
    }
    /* Persist */
    char snap_path[PB_PATH_MAX];
    snprintf(snap_path, sizeof(snap_path), "%s/snapshots.json", pb->path);
    snapshot_save(snap_path, pb->snapshots, pb->snapshot_count, pb->current_snapshot);
    ui_snapshot_bar_refresh();
    ui_app_show_toast(TR(TR_MSG_SNAP_SAVED));
}
static void new_snap_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();
    if (!ui_pedalboard_is_loaded()) {
        ui_app_show_message(TR(TR_MSG_NO_PB_LOADED_TITLE), TR(TR_MSG_NO_PB_LOADED), 2000);
        return;
    }
    ui_app_show_input(TR(TR_SNAP_NEW_TITLE), TR(TR_SNAP_NEW_HINT), do_new_snapshot, NULL);
}

/* ── Save snapshot ── */

static void do_save_snapshot(void *ud)
{
    (void)ud;
    ui_pedalboard_save_snapshot();
    ui_app_show_toast(TR(TR_MSG_SNAP_SAVED));
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

/* ─── Snapshot reorder overlay ───────────────────────────────────────────────── */

#define SNAP_ROW_H 60

typedef struct snap_reorder_s snap_reorder_t;

typedef struct {
    snap_reorder_t *state;
    int             snap_idx;
} snap_row_info_t;

typedef struct {
    snap_reorder_t *state;
    int             snap_idx;
} snap_action_ctx_t;

struct snap_reorder_s {
    lv_obj_t   *overlay;
    lv_obj_t   *list;
    int         count;
    int         drag_child;    /* list child index being dragged, -1 = none */
    lv_coord_t  drag_start_y;
    snap_row_info_t  *row_infos[PB_MAX_SNAPSHOTS];
    snap_action_ctx_t *act_ctxs[PB_MAX_SNAPSHOTS];
};

static snap_reorder_t *g_reorder = NULL;

static void reorder_save_snap(void)
{
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    char snap_path[PB_PATH_MAX];
    snprintf(snap_path, sizeof(snap_path), "%s/snapshots.json", pb->path);
    snapshot_save(snap_path, pb->snapshots, pb->snapshot_count, pb->current_snapshot);
    ui_snapshot_bar_refresh();
}

static void reorder_free_allocations(snap_reorder_t *state)
{
    for (int i = 0; i < PB_MAX_SNAPSHOTS; i++) {
        if (state->row_infos[i]) { free(state->row_infos[i]); state->row_infos[i] = NULL; }
        if (state->act_ctxs[i])  { free(state->act_ctxs[i]);  state->act_ctxs[i]  = NULL; }
    }
    state->count = 0;
}

static void reorder_rebuild(snap_reorder_t *state);  /* forward */

/* Rename a snapshot from within the reorder overlay */
static void snap_rename_done_cb(const char *name, void *ud)
{
    int *idx_p = ud;
    int idx = *idx_p;
    free(idx_p);
    if (!name || !name[0]) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || idx < 0 || idx >= pb->snapshot_count) return;
    pb_snapshot_rename(pb, idx, name);
    reorder_save_snap();
    if (g_reorder) reorder_rebuild(g_reorder);
}
static void snap_rename_btn_cb(lv_event_t *e)
{
    snap_action_ctx_t *ctx = lv_event_get_user_data(e);
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || ctx->snap_idx < 0 || ctx->snap_idx >= pb->snapshot_count) return;
    int *idx_copy = malloc(sizeof(int));
    *idx_copy = ctx->snap_idx;
    ui_app_show_input(TR(TR_SNAP_RENAME), pb->snapshots[ctx->snap_idx].name,
                      snap_rename_done_cb, idx_copy);
}

/* Delete a snapshot from within the reorder overlay */
static void snap_delete_done_cb(void *ud)
{
    int *idx_p = ud;
    int idx = *idx_p;
    free(idx_p);
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || idx < 0 || idx >= pb->snapshot_count) return;
    pb_snapshot_delete(pb, idx);
    reorder_save_snap();
    if (g_reorder) reorder_rebuild(g_reorder);
}
static void snap_delete_btn_cb(lv_event_t *e)
{
    snap_action_ctx_t *ctx = lv_event_get_user_data(e);
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || ctx->snap_idx < 0 || ctx->snap_idx >= pb->snapshot_count) return;
    char msg[256];
    snprintf(msg, sizeof(msg), TR(TR_CONFIRM_DELETE_SNAP),
             pb->snapshots[ctx->snap_idx].name);
    int *idx_copy = malloc(sizeof(int));
    *idx_copy = ctx->snap_idx;
    ui_app_show_confirm(TR(TR_SNAP_DELETE_TITLE), msg, snap_delete_done_cb, idx_copy);
}

/* Drag handle events */
static void snap_grab_event_cb(lv_event_t *e)
{
    snap_row_info_t *info = lv_event_get_user_data(e);
    snap_reorder_t *state = info->state;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *grab_btn = lv_event_get_target(e);
    lv_obj_t *row = lv_obj_get_parent(grab_btn);

    if (code == LV_EVENT_PRESSED) {
        state->drag_child = lv_obj_get_index(row);
        lv_indev_t *indev = lv_indev_active();
        lv_point_t pt; lv_indev_get_point(indev, &pt);
        state->drag_start_y = pt.y;
        lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), 0);
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_border_width(row, 1, 0);
    }
    else if (code == LV_EVENT_PRESSING && state->drag_child >= 0) {
        lv_indev_t *indev = lv_indev_active();
        lv_point_t pt; lv_indev_get_point(indev, &pt);

        lv_area_t list_coords;
        lv_obj_get_coords(state->list, &list_coords);
        int rel_y = (int)pt.y - (int)list_coords.y1;
        int target = rel_y / SNAP_ROW_H;
        if (target < 0) target = 0;
        if (target >= state->count) target = state->count - 1;

        int from = lv_obj_get_index(row);
        if (target != from) lv_obj_move_to_index(row, target);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (state->drag_child >= 0) {
            int final_pos = lv_obj_get_index(row);
            int orig_pos  = state->drag_child;
            state->drag_child = -1;
            lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            if (orig_pos != final_pos) {
                pedalboard_t *pb = ui_pedalboard_get();
                if (pb) { pb_snapshot_move(pb, orig_pos, final_pos); reorder_save_snap(); }
                reorder_rebuild(state);
            }
        }
    }
}

static void reorder_rebuild(snap_reorder_t *state)
{
    pedalboard_t *pb = ui_pedalboard_get();
    reorder_free_allocations(state);
    lv_obj_clean(state->list);
    if (!pb) return;

    state->count      = pb->snapshot_count;
    state->drag_child = -1;

    lv_color_t danger = lv_color_hex(0xC0392B);

    for (int i = 0; i < pb->snapshot_count; i++) {
        /* Row container */
        lv_obj_t *row = lv_obj_create(state->list);
        lv_obj_set_size(row, LV_PCT(100), SNAP_ROW_H);
        lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Allocate contexts */
        snap_row_info_t *rinfo = malloc(sizeof(*rinfo));
        rinfo->state = state; rinfo->snap_idx = i;
        state->row_infos[i] = rinfo;

        snap_action_ctx_t *actx = malloc(sizeof(*actx));
        actx->state = state; actx->snap_idx = i;
        state->act_ctxs[i] = actx;

        /* ≡ Grab handle */
        lv_obj_t *grab = lv_btn_create(row);
        lv_obj_set_size(grab, 52, SNAP_ROW_H);
        lv_obj_set_style_bg_color(grab, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_shadow_width(grab, 0, 0);
        lv_obj_set_style_radius(grab, 0, 0);
        lv_obj_clear_flag(grab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *grab_ico = lv_label_create(grab);
        lv_label_set_text(grab_ico, LV_SYMBOL_LIST);
        lv_obj_set_style_text_color(grab_ico, UI_COLOR_TEXT_DIM, 0);
        lv_obj_center(grab_ico);
        lv_obj_add_event_cb(grab, snap_grab_event_cb, LV_EVENT_PRESSED,    rinfo);
        lv_obj_add_event_cb(grab, snap_grab_event_cb, LV_EVENT_PRESSING,   rinfo);
        lv_obj_add_event_cb(grab, snap_grab_event_cb, LV_EVENT_RELEASED,   rinfo);
        lv_obj_add_event_cb(grab, snap_grab_event_cb, LV_EVENT_PRESS_LOST, rinfo);

        /* Name label */
        lv_obj_t *name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, pb->snapshots[i].name);
        lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name_lbl, 1);
        lv_obj_set_style_pad_left(name_lbl, 8, 0);

        /* ✎ Rename button */
        lv_obj_t *ren_btn = lv_btn_create(row);
        lv_obj_set_size(ren_btn, 54, SNAP_ROW_H);
        lv_obj_set_style_bg_color(ren_btn, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_radius(ren_btn, 0, 0);
        lv_obj_set_style_shadow_width(ren_btn, 0, 0);
        lv_obj_t *ren_ico = lv_label_create(ren_btn);
        lv_label_set_text(ren_ico, LV_SYMBOL_EDIT);
        lv_obj_set_style_text_color(ren_ico, lv_color_white(), 0);
        lv_obj_center(ren_ico);
        lv_obj_add_event_cb(ren_btn, snap_rename_btn_cb, LV_EVENT_CLICKED, actx);

        /* 🗑 Delete button */
        lv_obj_t *del_btn = lv_btn_create(row);
        lv_obj_set_size(del_btn, 54, SNAP_ROW_H);
        lv_obj_set_style_bg_color(del_btn, danger, 0);
        lv_obj_set_style_radius(del_btn, 0, 0);
        lv_obj_set_style_shadow_width(del_btn, 0, 0);
        lv_obj_t *del_ico = lv_label_create(del_btn);
        lv_label_set_text(del_ico, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(del_ico, lv_color_white(), 0);
        lv_obj_center(del_ico);
        lv_obj_add_event_cb(del_btn, snap_delete_btn_cb, LV_EVENT_CLICKED, actx);
    }
}

static void reorder_overlay_delete_cb(lv_event_t *e)
{
    (void)e;
    if (g_reorder) {
        reorder_free_allocations(g_reorder);
        free(g_reorder);
        g_reorder = NULL;
    }
}

static void btn_save_cb(lv_event_t *e);  /* forward — defined later */

static void reopen_save_menu_cb(void *arg)
{
    (void)arg;
    btn_save_cb(NULL);
}

static void reorder_close_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_reorder) {
        lv_obj_t *ov = g_reorder->overlay;
        reorder_free_allocations(g_reorder);
        /* Let LV_EVENT_DELETE handler free g_reorder itself */
        lv_obj_delete_async(ov);
        /* Return to the Files menu after the overlay is gone */
        lv_async_call(reopen_save_menu_cb, NULL);
    }
}

static void reorder_dim_click_cb(lv_event_t *e)
{
    /* Close only if click landed directly on the dim overlay (not on the panel) */
    if (lv_event_get_target(e) == lv_event_get_current_target(e))
        reorder_close_btn_cb(e);
}

static void reorder_snaps_cb(lv_event_t *e)
{
    (void)e;
    save_menu_close_async();

    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !ui_pedalboard_is_loaded()) {
        ui_app_show_message(TR(TR_MSG_NO_PB_LOADED_TITLE), TR(TR_MSG_NO_PB_LOADED), 2000);
        return;
    }
    if (pb->snapshot_count == 0) {
        ui_app_show_message(TR(TR_MSG_NO_SNAP), TR(TR_MSG_SELECT_SNAP_FIRST), 2000);
        return;
    }

    snap_reorder_t *state = calloc(1, sizeof(*state));
    state->drag_child = -1;
    g_reorder = state;

    /* Full-screen dim overlay */
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, reorder_dim_click_cb,    LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(overlay, reorder_overlay_delete_cb, LV_EVENT_DELETE, NULL);
    state->overlay = overlay;

    /* Centered panel */
    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 660, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(panel);
    lv_obj_set_size(hdr, LV_PCT(100), 44);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, TR(TR_SNAP_REORDER_TITLE));
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *close_btn = lv_btn_create(hdr);
    lv_obj_set_size(close_btn, 44, 44);
    lv_obj_set_style_bg_color(close_btn, UI_COLOR_BYPASS, 0);
    lv_obj_set_style_radius(close_btn, 4, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *close_ico = lv_label_create(close_btn);
    lv_label_set_text(close_ico, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_ico, lv_color_white(), 0);
    lv_obj_center(close_ico);
    lv_obj_add_event_cb(close_btn, reorder_close_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Scrollable list of rows */
    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(list, 380, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    state->list = list;

    reorder_rebuild(state);
}

/* ── Redesigned save menu (two-column) ── */

/* Helper: create a button in a save menu column */
static lv_obj_t *save_col_btn(lv_obj_t *col, const char *icon, const char *text,
                               lv_color_t bg, bool outlined, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(col);
    lv_obj_set_size(btn, LV_PCT(100), 50);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    if (outlined) {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(btn, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
    }
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text_fmt(lbl, "%s  %s", icon, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, LV_PCT(92));
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* Section header label for save menu columns */
static void save_col_section(lv_obj_t *col, const char *text)
{
    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_bottom(lbl, 4, 0);
}

static void btn_save_cb(lv_event_t *e)
{
    (void)e;
    if (g_save_menu) { save_menu_close(); return; }

    /* Full-screen transparent overlay — click outside panel closes the menu */
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_event_cb(overlay, save_menu_overlay_cb, LV_EVENT_CLICKED, NULL);
    g_save_menu = overlay;

    /* Centered panel */
    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 760, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Header row */
    lv_obj_t *hdr = lv_obj_create(panel);
    lv_obj_set_size(hdr, LV_PCT(100), 44);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, TR(TR_MENU_FILES));
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *hdr_close = lv_btn_create(hdr);
    lv_obj_set_size(hdr_close, 44, 44);
    lv_obj_set_style_bg_color(hdr_close, UI_COLOR_BYPASS, 0);
    lv_obj_set_style_radius(hdr_close, 4, 0);
    lv_obj_set_style_shadow_width(hdr_close, 0, 0);
    lv_obj_align(hdr_close, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *hdr_close_ico = lv_label_create(hdr_close);
    lv_label_set_text(hdr_close_ico, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(hdr_close_ico, lv_color_white(), 0);
    lv_obj_center(hdr_close_ico);
    lv_obj_add_event_cb(hdr_close, save_menu_overlay_cb, LV_EVENT_CLICKED, NULL);

    /* Divider */
    lv_obj_t *div = lv_obj_create(panel);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);
    (void)div;

    /* Two-column content row */
    lv_obj_t *cols = lv_obj_create(panel);
    lv_obj_set_size(cols, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cols, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cols, 0, 0);
    lv_obj_set_style_pad_all(cols, 0, 0);
    lv_obj_set_style_pad_column(cols, 16, 0);
    lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cols, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(cols, LV_OBJ_FLAG_SCROLLABLE);

    lv_color_t green  = lv_color_hex(0x27AE60);
    lv_color_t danger = lv_color_hex(0xC0392B);

    /* ── Left column: Pedalboard ── */
    lv_obj_t *left = lv_obj_create(cols);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_row(left, 6, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    save_col_section(left, "Pedalboard");
    save_col_btn(left, LV_SYMBOL_PLUS,     TR(TR_MENU_NEW_PB),     green,            false, new_pb_cb);
    save_col_btn(left, LV_SYMBOL_SAVE,     TR(TR_MENU_SAVE_PB),    UI_COLOR_ACCENT,  false, confirm_save_pedalboard_cb);
    save_col_btn(left, LV_SYMBOL_COPY,     TR(TR_MENU_SAVE_PB_AS), UI_COLOR_SURFACE, true,  save_as_cb);
    save_col_btn(left, LV_SYMBOL_EDIT,     TR(TR_MENU_RENAME_PB),  UI_COLOR_SURFACE, true,  rename_pb_cb);
    save_col_btn(left, LV_SYMBOL_TRASH,    TR(TR_MENU_DELETE_PB),  danger,           false, confirm_delete_pedalboard_cb);

    /* Vertical separator */
    lv_obj_t *vsep = lv_obj_create(cols);
    lv_obj_set_size(vsep, 1, LV_PCT(100));
    lv_obj_set_style_bg_color(vsep, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(vsep, 0, 0);
    lv_obj_set_style_radius(vsep, 0, 0);
    lv_obj_set_style_pad_all(vsep, 0, 0);
    (void)vsep;

    /* ── Right column: Snapshot ── */
    lv_obj_t *right = lv_obj_create(cols);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_row(right, 6, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    save_col_section(right, "Snapshot");
    save_col_btn(right, LV_SYMBOL_PLUS,     TR(TR_MENU_NEW_SNAP),     green,           false, new_snap_cb);
    save_col_btn(right, LV_SYMBOL_SAVE,     TR(TR_MENU_SAVE_SNAP),    UI_COLOR_PRIMARY, false, confirm_save_snapshot_cb);
    save_col_btn(right, LV_SYMBOL_LIST,     TR(TR_MENU_REORDER_SNAPS),UI_COLOR_SURFACE, true,  reorder_snaps_cb);
}

static void btn_settings_cb(lv_event_t *e)
{
    (void)e;
    if (g_current_screen == UI_SCREEN_SETTINGS)
        ui_app_show_screen(UI_SCREEN_PEDALBOARD);
    else
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
        lv_color_hex(0x1A7A4A), btn_pre_fx_cb, &g_prefx_label);
    lv_obj_align(btn_pre_fx, LV_ALIGN_LEFT_MID, UI_TOP_BTN_SZ + 6, 0);

    lv_obj_t *btn_cond = make_top_btn(g_top_bar,
        LV_SYMBOL_AUDIO, TR(TR_CONDUCTOR_TITLE),
        lv_color_hex(0x6A4C9C), btn_conductor_cb, &g_cond_label);
    lv_obj_align(btn_cond, LV_ALIGN_LEFT_MID, (UI_TOP_BTN_SZ + 6) * 2, 0);

    lv_obj_t *btn_scene = make_top_btn(g_top_bar,
        LV_SYMBOL_SHUFFLE, TR(TR_SCENE_TITLE),
        lv_color_hex(0x1A6B7A), btn_scene_cb, &g_scene_label);
    lv_obj_align(btn_scene, LV_ALIGN_LEFT_MID, (UI_TOP_BTN_SZ + 6) * 3, 0);

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
        UI_COLOR_SURFACE, btn_settings_cb, &g_set_label);
    lv_obj_set_style_border_color(btn_set, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(btn_set, 1, 0);
    lv_obj_align(btn_set, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *btn_save = make_top_btn(g_top_bar,
        LV_SYMBOL_SAVE, TR(TR_MENU_FILES),
        UI_COLOR_ACCENT, btn_save_cb, &g_save_label);
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
    ui_scene_close();
    /* Free reorder C allocations before lv_obj_clean deletes the LVGL objects.
     * LV_EVENT_DELETE on g_reorder->overlay will free g_reorder itself. */
    if (g_reorder) reorder_free_allocations(g_reorder);
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
    ui_app_keyboard_apply_lang(kbd);

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
    /* Update persistent top-bar labels */
    if (g_banks_label)  lv_label_set_text(g_banks_label,  TR(TR_BANKS));
    if (g_prefx_label)  lv_label_set_text(g_prefx_label,  TR(TR_PREFX_TITLE));
    if (g_cond_label)   lv_label_set_text(g_cond_label,   TR(TR_CONDUCTOR_TITLE));
    if (g_scene_label)  lv_label_set_text(g_scene_label,  TR(TR_SCENE_TITLE));
    if (g_set_label)    lv_label_set_text(g_set_label,    TR(TR_SETTINGS_TITLE));
    if (g_save_label)   lv_label_set_text(g_save_label,   TR(TR_MENU_FILES));

    /* Update snapshot bar prefix */
    ui_snapshot_bar_update_lang();

    /* Rebuild the current content screen with new strings */
    ui_app_show_screen(g_current_screen);
}

void ui_app_keyboard_apply_lang(lv_obj_t *kbd)
{
    if (i18n_get_lang() == LANG_FR) {
        lv_keyboard_set_map(kbd, LV_KEYBOARD_MODE_TEXT_LOWER, azerty_map_lc, azerty_ctrl_lc);
        lv_keyboard_set_map(kbd, LV_KEYBOARD_MODE_TEXT_UPPER, azerty_map_uc, azerty_ctrl_uc);
    }
}
