#include "ui_scene.h"
#include "ui_app.h"
#include "ui_pedalboard.h"
#include "../pedalboard.h"
#include "../host_comm.h"
#include "../bank.h"
#include "../settings.h"
#include "../i18n.h"
#include "cJSON.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Constants ──────────────────────────────────────────────────────────────── */
#define SLOT_COUNT 4
#define GLOW_COLOR  lv_color_hex(0x2ECC71)
#define GLOW_W      24
#define GLOW_SPREAD 8

/* ─── Slot state ─────────────────────────────────────────────────────────────── */
typedef struct {
    int       instance_id;
    lv_obj_t *card;
    lv_obj_t *name_lbl;
    lv_obj_t *state_lbl;
    lv_obj_t *cancel_btn;
} slot_t;

/* ─── Module state ───────────────────────────────────────────────────────────── */
static lv_obj_t *g_overlay         = NULL;
static lv_obj_t *g_content_pedals  = NULL;
static lv_obj_t *g_content_setlist = NULL;
static lv_obj_t *g_tab_btn_pedals  = NULL;
static lv_obj_t *g_tab_btn_setlist = NULL;
static slot_t    g_slots[SLOT_COUNT];
static int       g_learning_slot   = -1;
static int       g_active_tab      = 0;
static lv_obj_t *g_spinner         = NULL;
static int       g_bank_idx        = -1;   /* -1 = all banks */
static lv_obj_t *g_bank_menu       = NULL;

/* ─── Forward declarations ───────────────────────────────────────────────────── */
static void slot_apply_style(int idx);
static void build_pedals_tab(lv_obj_t *parent);
static void build_setlist_tab(lv_obj_t *parent);
static void scene_refresh(void);
static void show_spinner(void);
static void hide_spinner(void);
static void open_bank_menu(lv_obj_t *anchor, bank_list_t *banks);

/* ─── Close callback shim ────────────────────────────────────────────────────── */
static void scene_close_cb(lv_event_t *e) { (void)e; ui_scene_close(); }

/* ─── scene.json persistence ─────────────────────────────────────────────────── */

static void scene_save(void)
{
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !pb->path[0]) return;

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "slots");
    for (int i = 0; i < SLOT_COUNT; i++)
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(g_slots[i].instance_id));

    char path[PB_PATH_MAX];
    snprintf(path, sizeof(path), "%s/scene.json", pb->path);
    char *js = cJSON_Print(root);
    if (js) {
        FILE *f = fopen(path, "w");
        if (f) { fputs(js, f); fclose(f); }
        free(js);
    }
    cJSON_Delete(root);
}

static void scene_load(void)
{
    for (int i = 0; i < SLOT_COUNT; i++)
        g_slots[i].instance_id = -1;

    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || !pb->path[0]) return;

    char path[PB_PATH_MAX];
    snprintf(path, sizeof(path), "%s/scene.json", pb->path);
    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f); rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;

    cJSON *arr = cJSON_GetObjectItem(root, "slots");
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        if (n > SLOT_COUNT) n = SLOT_COUNT;
        for (int i = 0; i < n; i++) {
            cJSON *v = cJSON_GetArrayItem(arr, i);
            if (cJSON_IsNumber(v))
                g_slots[i].instance_id = (int)v->valuedouble;
        }
    }
    cJSON_Delete(root);
}

/* ─── scene_state.json persistence (bank selection) ─────────────────────────── */

static void scene_bank_save(void)
{
    mpt_settings_t *s = settings_get();
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", s->banks_file);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';

    char path[580];
    snprintf(path, sizeof(path), "%s/scene_state.json", dir);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "bank_idx", g_bank_idx);
    char *js = cJSON_Print(root);
    if (js) {
        FILE *f = fopen(path, "w");
        if (f) { fputs(js, f); fclose(f); }
        free(js);
    }
    cJSON_Delete(root);
}

static void scene_bank_load(void)
{
    g_bank_idx = -1;
    mpt_settings_t *s = settings_get();
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", s->banks_file);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';

    char path[580];
    snprintf(path, sizeof(path), "%s/scene_state.json", dir);

    FILE *f = fopen(path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f); rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;
    cJSON *ji = cJSON_GetObjectItem(root, "bank_idx");
    if (cJSON_IsNumber(ji))
        g_bank_idx = (int)ji->valuedouble;
    cJSON_Delete(root);
}

/* ─── Bank menu ──────────────────────────────────────────────────────────────── */

typedef struct {
    int       bank_idx;   /* -1 = all banks */
    lv_obj_t *menu;       /* backdrop to close on pick */
} bank_item_ctx_t;

static void bank_item_delete_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void bank_item_cb(lv_event_t *e)
{
    bank_item_ctx_t *ctx = lv_event_get_user_data(e);
    int new_idx    = ctx->bank_idx;
    lv_obj_t *menu = ctx->menu;

    lv_obj_delete_async(menu);
    g_bank_menu = NULL;

    if (g_bank_idx == new_idx) return;
    g_bank_idx = new_idx;
    scene_bank_save();

    if (!g_content_setlist) return;
    lv_obj_clean(g_content_setlist);
    build_setlist_tab(g_content_setlist);
    if (g_active_tab == 1) show_tab(1);
}

static void bank_menu_backdrop_cb(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target(e);
    lv_obj_t *c = lv_event_get_current_target(e);
    if (t == c) {
        lv_obj_delete_async(c);
        g_bank_menu = NULL;
    }
}

static void open_bank_menu(lv_obj_t *anchor, bank_list_t *banks)
{
    if (g_bank_menu) {
        lv_obj_delete_async(g_bank_menu);
        g_bank_menu = NULL;
        return;
    }

    lv_obj_t *backdrop = lv_obj_create(g_overlay);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(backdrop, bank_menu_backdrop_cb, LV_EVENT_CLICKED, NULL);
    g_bank_menu = backdrop;

    /* Dropdown panel just below the anchor button */
    lv_area_t area;
    lv_obj_get_coords(anchor, &area);
    int px = area.x1;
    int py = area.y2 + 4;
    int pw = area.x2 - area.x1;
    if (pw < 220) pw = 220;

    lv_obj_t *panel = lv_obj_create(backdrop);
    lv_obj_set_pos(panel, px, py);
    lv_obj_set_size(panel, pw, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, 480, 0);
    lv_obj_set_style_bg_color(panel, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_style_shadow_width(panel, 14, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_40, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, (lv_event_cb_t)lv_event_stop_bubbling, LV_EVENT_CLICKED, NULL);

#define BANK_ITEM(label_str, bidx) do { \
    bool active = (g_bank_idx == (bidx)); \
    lv_obj_t *b = lv_btn_create(panel); \
    lv_obj_set_size(b, LV_PCT(100), 50); \
    lv_obj_set_style_bg_color(b, active ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE, 0); \
    lv_obj_set_style_radius(b, 6, 0); \
    lv_obj_set_style_shadow_width(b, 0, 0); \
    bank_item_ctx_t *ctx = malloc(sizeof(*ctx)); \
    ctx->bank_idx = (bidx); ctx->menu = backdrop; \
    lv_obj_add_event_cb(b, bank_item_cb,        LV_EVENT_CLICKED, ctx); \
    lv_obj_add_event_cb(b, bank_item_delete_cb, LV_EVENT_DELETE,  ctx); \
    lv_obj_t *l = lv_label_create(b); \
    lv_label_set_text(l, (label_str)); \
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT, 0); \
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0); \
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); \
    lv_obj_set_width(l, LV_PCT(90)); \
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0); \
} while(0)

    BANK_ITEM(TR(TR_SCENE_ALL_BANKS), -1);
    for (int bi = 0; bi < banks->bank_count; bi++)
        BANK_ITEM(banks->banks[bi].name, bi);

#undef BANK_ITEM
}

/* ─── Slot style ─────────────────────────────────────────────────────────────── */

static void slot_apply_style(int idx)
{
    slot_t *sl = &g_slots[idx];
    if (!sl->card) return;

    pedalboard_t *pb = ui_pedalboard_get();
    pb_plugin_t  *pl = (pb && sl->instance_id >= 0)
                        ? pb_find_plugin(pb, sl->instance_id) : NULL;

    if (g_learning_slot == idx) {
        lv_obj_set_style_bg_color(sl->card, lv_color_hex(0x1A3A5C), 0);
        lv_obj_set_style_shadow_color(sl->card, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_shadow_width(sl->card, 18, 0);
        lv_obj_set_style_shadow_spread(sl->card, 6, 0);
        lv_obj_set_style_shadow_opa(sl->card, LV_OPA_80, 0);
        lv_obj_set_style_shadow_ofs_x(sl->card, 0, 0);
        lv_obj_set_style_shadow_ofs_y(sl->card, 0, 0);
        if (sl->name_lbl)  lv_label_set_text(sl->name_lbl,  pl ? pl->label : "");
        if (sl->state_lbl) {
            lv_label_set_text(sl->state_lbl, TR(TR_SCENE_MIDI_LEARNING));
            lv_obj_set_style_text_color(sl->state_lbl, UI_COLOR_ACCENT, 0);
        }
        if (sl->cancel_btn) lv_obj_clear_flag(sl->cancel_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (sl->cancel_btn) lv_obj_add_flag(sl->cancel_btn, LV_OBJ_FLAG_HIDDEN);

    if (!pl || sl->instance_id < 0) {
        lv_obj_set_style_bg_color(sl->card, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_shadow_width(sl->card, 0, 0);
        if (sl->name_lbl)  lv_label_set_text(sl->name_lbl,  TR(TR_SCENE_UNASSIGNED));
        if (sl->state_lbl) lv_label_set_text(sl->state_lbl, "");
        if (sl->instance_id >= 0 && !pl)
            g_slots[idx].instance_id = -1; /* stale id */
        return;
    }

    if (pl->enabled) {
        lv_obj_set_style_bg_color(sl->card, lv_color_hex(0x163320), 0);
        lv_obj_set_style_shadow_color(sl->card, GLOW_COLOR, 0);
        lv_obj_set_style_shadow_width(sl->card, GLOW_W, 0);
        lv_obj_set_style_shadow_spread(sl->card, GLOW_SPREAD, 0);
        lv_obj_set_style_shadow_opa(sl->card, LV_OPA_80, 0);
        lv_obj_set_style_shadow_ofs_x(sl->card, 0, 0);
        lv_obj_set_style_shadow_ofs_y(sl->card, 0, 0);
        if (sl->name_lbl) lv_label_set_text(sl->name_lbl, pl->label);
        if (sl->state_lbl) {
            lv_label_set_text(sl->state_lbl, TR(TR_SCENE_ACTIVE));
            lv_obj_set_style_text_color(sl->state_lbl, GLOW_COLOR, 0);
        }
    } else {
        lv_obj_set_style_bg_color(sl->card, lv_color_hex(0x222222), 0);
        lv_obj_set_style_shadow_width(sl->card, 0, 0);
        if (sl->name_lbl) lv_label_set_text(sl->name_lbl, pl->label);
        if (sl->state_lbl) {
            lv_label_set_text(sl->state_lbl, TR(TR_SCENE_BYPASSED));
            lv_obj_set_style_text_color(sl->state_lbl, UI_COLOR_TEXT_DIM, 0);
        }
    }
}

/* ─── Cancel MIDI learn ──────────────────────────────────────────────────────── */

static void cancel_learn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_learning_slot != idx) return;
    host_midi_unmap(g_slots[idx].instance_id, ":bypass");
    g_learning_slot = -1;
    slot_apply_style(idx);
}

/* ─── Plugin picker overlay ──────────────────────────────────────────────────── */

typedef struct {
    int       slot_idx;
    int       instance_id;
    lv_obj_t *backdrop;
} picker_item_ctx_t;

static void picker_item_cb(lv_event_t *e)
{
    picker_item_ctx_t *ctx = lv_event_get_user_data(e);
    int slot = ctx->slot_idx;
    int iid  = ctx->instance_id;
    lv_obj_t *backdrop = ctx->backdrop;

    /* Close picker first — free all item contexts via LV_EVENT_DELETE on backdrop */
    lv_obj_delete_async(backdrop);

    if (slot < 0 || slot >= SLOT_COUNT || iid < 0) return;

    g_slots[slot].instance_id = iid;
    g_learning_slot = slot;
    slot_apply_style(slot);
    scene_save();
    host_midi_learn(iid, ":bypass", 0.0f, 1.0f);
}

static void picker_item_delete_cb(lv_event_t *e)
{
    picker_item_ctx_t *ctx = lv_event_get_user_data(e);
    free(ctx);
}

static void picker_backdrop_cb(lv_event_t *e)
{
    lv_obj_t *target  = lv_event_get_target(e);
    lv_obj_t *current = lv_event_get_current_target(e);
    if (target == current) /* only backdrop itself, not children */
        lv_obj_delete_async(current);
}

static void open_plugin_picker(int slot_idx)
{
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb || pb->plugin_count == 0) {
        ui_app_show_toast(TR(TR_SCENE_NO_PB));
        return;
    }

    lv_obj_t *backdrop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(backdrop, picker_backdrop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(backdrop);
    lv_obj_set_size(panel, 480, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, 540, 0);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, (lv_event_cb_t)lv_event_stop_bubbling, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, TR(TR_SCENE_PICK_PLUGIN));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(list, 420, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < pb->plugin_count; i++) {
        pb_plugin_t *pl = &pb->plugins[i];
        lv_obj_t *btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), 58);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        picker_item_ctx_t *ctx = malloc(sizeof(*ctx));
        ctx->slot_idx   = slot_idx;
        ctx->instance_id = pl->instance_id;
        ctx->backdrop   = backdrop;
        lv_obj_add_event_cb(btn, picker_item_cb,     LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(btn, picker_item_delete_cb, LV_EVENT_DELETE, ctx);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, pl->label);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    }
}

/* ─── Slot context menu (long press) ────────────────────────────────────────── */

typedef struct {
    int       slot_idx;
    lv_obj_t *backdrop;
} slot_menu_ctx_t;

static void slot_menu_item_delete_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void slot_unassign_cb(lv_event_t *e)
{
    slot_menu_ctx_t *ctx = lv_event_get_user_data(e);
    int idx        = ctx->slot_idx;
    lv_obj_t *back = ctx->backdrop;
    lv_obj_delete_async(back);
    if (g_learning_slot == idx) {
        host_midi_unmap(g_slots[idx].instance_id, ":bypass");
        g_learning_slot = -1;
    }
    g_slots[idx].instance_id = -1;
    slot_apply_style(idx);
    scene_save();
}

static void slot_learn_again_cb(lv_event_t *e)
{
    slot_menu_ctx_t *ctx = lv_event_get_user_data(e);
    int idx        = ctx->slot_idx;
    lv_obj_t *back = ctx->backdrop;
    lv_obj_delete_async(back);
    g_learning_slot = idx;
    slot_apply_style(idx);
    host_midi_learn(g_slots[idx].instance_id, ":bypass", 0.0f, 1.0f);
}

static void slot_menu_backdrop_click_cb(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target(e);
    lv_obj_t *c = lv_event_get_current_target(e);
    if (t == c) lv_obj_delete_async(c);
}

static void open_slot_menu(int slot_idx)
{
    lv_obj_t *backdrop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(backdrop, slot_menu_backdrop_click_cb, LV_EVENT_CLICKED, NULL);

    /* Position panel near slot card center */
    slot_t *sl = &g_slots[slot_idx];
    lv_area_t area; lv_obj_get_coords(sl->card, &area);
    int cx = (area.x1 + area.x2) / 2;
    int cy = (area.y1 + area.y2) / 2;
    int px = cx - 115, py = cy - 60;
    if (px < 4) px = 4;
    if (py < 70) py = 70;
    if (px + 230 > 1276) px = 1276 - 230;

    lv_obj_t *panel = lv_obj_create(backdrop);
    lv_obj_set_size(panel, 230, LV_SIZE_CONTENT);
    lv_obj_set_pos(panel, px, py);
    lv_obj_set_style_bg_color(panel, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, (lv_event_cb_t)lv_event_stop_bubbling, LV_EVENT_CLICKED, NULL);

#define MENU_BTN(bg, text, cb) do { \
    lv_obj_t *b = lv_btn_create(panel); \
    lv_obj_set_size(b, LV_PCT(100), 50); \
    lv_obj_set_style_bg_color(b, bg, 0); \
    lv_obj_set_style_radius(b, 6, 0); \
    lv_obj_set_style_shadow_width(b, 0, 0); \
    slot_menu_ctx_t *mc = malloc(sizeof(*mc)); \
    mc->slot_idx = slot_idx; mc->backdrop = backdrop; \
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, mc); \
    lv_obj_add_event_cb(b, slot_menu_item_delete_cb, LV_EVENT_DELETE, mc); \
    lv_obj_t *l = lv_label_create(b); \
    lv_label_set_text(l, text); \
    lv_obj_set_style_text_color(l, lv_color_white(), 0); \
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0); \
    lv_obj_center(l); \
} while(0)

    MENU_BTN(UI_COLOR_ACCENT,            TR(TR_SCENE_LEARN_MIDI), slot_learn_again_cb);
    MENU_BTN(lv_color_hex(0xC0392B),     TR(TR_SCENE_UNASSIGN),   slot_unassign_cb);

#undef MENU_BTN
}

/* ─── Slot tap + long-press callbacks ───────────────────────────────────────── */

static void slot_tap_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_learning_slot == idx) return;

    if (g_slots[idx].instance_id < 0) {
        open_plugin_picker(idx);
        return;
    }

    /* Toggle bypass */
    pedalboard_t *pb = ui_pedalboard_get();
    pb_plugin_t  *pl = pb ? pb_find_plugin(pb, g_slots[idx].instance_id) : NULL;
    if (!pl) return;
    bool new_enabled = !pl->enabled;
    host_bypass(pl->instance_id, new_enabled ? 0 : 1);
    pl->enabled  = new_enabled;
    pb->modified = true;
    slot_apply_style(idx);
}

static void slot_long_press_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_slots[idx].instance_id < 0 || g_learning_slot == idx) return;
    open_slot_menu(idx);
}

/* ─── Pedals tab ─────────────────────────────────────────────────────────────── */

static void build_pedals_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 20, 0);
    lv_obj_set_style_pad_column(parent, 16, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < SLOT_COUNT; i++) {
        slot_t *sl = &g_slots[i];

        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_height(card, 520);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_shadow_ofs_x(card, 0, 0);
        lv_obj_set_style_shadow_ofs_y(card, 0, 0);
        lv_obj_set_style_pad_all(card, 16, 0);
        lv_obj_set_style_pad_row(card, 12, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, slot_tap_cb,        LV_EVENT_CLICKED,     (void *)(intptr_t)i);
        lv_obj_add_event_cb(card, slot_long_press_cb, LV_EVENT_LONG_PRESSED,(void *)(intptr_t)i);
        sl->card = card;

        /* Slot number */
        lv_obj_t *badge = lv_label_create(card);
        lv_label_set_text_fmt(badge, "%d", i + 1);
        lv_obj_set_style_text_color(badge, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(badge, &lv_font_montserrat_14, 0);

        /* Plugin name */
        lv_obj_t *name_lbl = lv_label_create(card);
        lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(name_lbl, LV_PCT(100));
        lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
        sl->name_lbl = name_lbl;

        /* State: ON / OFF / "Attente MIDI..." */
        lv_obj_t *state_lbl = lv_label_create(card);
        lv_obj_set_style_text_font(state_lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_align(state_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(state_lbl, LV_PCT(100));
        sl->state_lbl = state_lbl;

        /* Cancel MIDI learn button (hidden by default) */
        lv_obj_t *cancel_btn = lv_btn_create(card);
        lv_obj_set_size(cancel_btn, LV_PCT(75), 46);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xC0392B), 0);
        lv_obj_set_style_radius(cancel_btn, 8, 0);
        lv_obj_set_style_shadow_width(cancel_btn, 0, 0);
        lv_obj_add_event_cb(cancel_btn, cancel_learn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
        lv_label_set_text(cancel_lbl, TR(TR_SCENE_CANCEL_LEARN));
        lv_obj_set_style_text_color(cancel_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(cancel_lbl);
        sl->cancel_btn = cancel_btn;

        slot_apply_style(i);
    }
}

/* ─── Bank button callbacks (used in build_setlist_tab) ─────────────────────── */

static void bank_list_delete_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void bank_btn_cb(lv_event_t *e)
{
    bank_list_t *banks = lv_event_get_user_data(e);
    open_bank_menu(lv_event_get_target(e), banks);
}

/* ─── Setlist tab ────────────────────────────────────────────────────────────── */

typedef struct {
    char     bundle[PB_PATH_MAX];
    lv_obj_t *overlay;
} pb_confirm_ctx_t;

static void pb_confirm_delete_cb(lv_event_t *e)
{
    pb_confirm_ctx_t *ctx = lv_event_get_user_data(e);
    free(ctx);
}

static void pb_confirm_cancel_cb(lv_event_t *e)
{
    pb_confirm_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_delete_async(ctx->overlay);
    /* ctx freed by LV_EVENT_DELETE on this button */
}

static void show_spinner(void)
{
    if (!g_overlay || g_spinner) return;
    /* Dim overlay over the content */
    g_spinner = lv_obj_create(g_overlay);
    lv_obj_add_flag(g_spinner, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(g_spinner, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_spinner, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_spinner, LV_OPA_50, 0);
    lv_obj_set_style_border_width(g_spinner, 0, 0);
    lv_obj_set_pos(g_spinner, 0, 0);
    lv_obj_clear_flag(g_spinner, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *arc = lv_spinner_create(g_spinner);
    lv_obj_set_size(arc, 80, 80);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_color(arc, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, UI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
}

static void hide_spinner(void)
{
    if (g_spinner) { lv_obj_del(g_spinner); g_spinner = NULL; }
}

static void pb_do_load(void *arg)
{
    char *bundle = arg;
    ui_pedalboard_load(bundle, NULL, NULL);
    free(bundle);
    scene_refresh();
}

static void pb_confirm_ok_cb(lv_event_t *e)
{
    pb_confirm_ctx_t *ctx = lv_event_get_user_data(e);
    char *bundle = strdup(ctx->bundle);
    lv_obj_delete_async(ctx->overlay);
    /* ctx freed by LV_EVENT_DELETE on this button */
    show_spinner();
    lv_async_call(pb_do_load, bundle);
}

static void pb_list_item_cb(lv_event_t *e)
{
    const char *bundle = lv_event_get_user_data(e);
    if (!bundle) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (pb && strcmp(pb->path, bundle) == 0) return; /* already current */

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *box = lv_obj_create(overlay);
    lv_obj_set_size(box, 460, LV_SIZE_CONTENT);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(box, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 20, 0);
    lv_obj_set_style_pad_row(box, 14, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, (lv_event_cb_t)lv_event_stop_bubbling, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, TR(TR_SCENE_CONFIRM_PB));
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);

    lv_obj_t *row = lv_obj_create(box);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);

    pb_confirm_ctx_t *ctx = malloc(sizeof(*ctx));
    snprintf(ctx->bundle, sizeof(ctx->bundle), "%s", bundle);
    ctx->overlay = overlay;

    lv_obj_t *btn_no = lv_btn_create(row);
    lv_obj_set_size(btn_no, 120, 46);
    lv_obj_set_style_bg_color(btn_no, UI_COLOR_BYPASS, 0);
    lv_obj_set_style_radius(btn_no, 6, 0);
    lv_obj_set_style_shadow_width(btn_no, 0, 0);
    lv_obj_add_event_cb(btn_no, pb_confirm_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(btn_no, pb_confirm_delete_cb, LV_EVENT_DELETE,  ctx);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, TR(TR_CANCEL));
    lv_obj_set_style_text_color(lbl_no, UI_COLOR_TEXT, 0);
    lv_obj_center(lbl_no);

    pb_confirm_ctx_t *ctx2 = malloc(sizeof(*ctx2));
    *ctx2 = *ctx;
    lv_obj_t *btn_yes = lv_btn_create(row);
    lv_obj_set_size(btn_yes, 120, 46);
    lv_obj_set_style_bg_color(btn_yes, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(btn_yes, 6, 0);
    lv_obj_set_style_shadow_width(btn_yes, 0, 0);
    lv_obj_add_event_cb(btn_yes, pb_confirm_ok_cb,     LV_EVENT_CLICKED, ctx2);
    lv_obj_add_event_cb(btn_yes, pb_confirm_delete_cb, LV_EVENT_DELETE,  ctx2);
    lv_obj_t *lbl_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_yes, TR(TR_OK));
    lv_obj_set_style_text_color(lbl_yes, lv_color_white(), 0);
    lv_obj_center(lbl_yes);
}

static void pb_list_item_delete_cb(lv_event_t *e)
{
    char *s = lv_event_get_user_data(e);
    free(s);
}

#define SNAP_H_BASE 100
#define SNAP_H_CUR  ((SNAP_H_BASE * 13) / 10)

static void snap_item_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_pedalboard_apply_snapshot(idx);
    /* Refresh height, colors and shadow of all snap cards */
    lv_obj_t *list = lv_obj_get_parent(lv_event_get_target(e));
    if (!list) return;
    pedalboard_t *pb = ui_pedalboard_get();
    if (!pb) return;
    int n = lv_obj_get_child_count(list);
    for (int i = 0; i < n; i++) {
        lv_obj_t *card = lv_obj_get_child(list, i);
        bool cur = (i == pb->current_snapshot);
        lv_obj_set_height(card, cur ? SNAP_H_CUR : SNAP_H_BASE);
        lv_obj_set_style_bg_color(card,
            cur ? lv_color_hex(0x252525) : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(card,
            cur ? UI_COLOR_PRIMARY : lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(card, cur ? 2 : 1, 0);
        lv_obj_set_style_shadow_color(card, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_shadow_width(card,  cur ? 22 : 0, 0);
        lv_obj_set_style_shadow_spread(card, cur ? 6  : 0, 0);
        lv_obj_set_style_shadow_opa(card,    cur ? LV_OPA_60 : LV_OPA_TRANSP, 0);
        /* Update name label (first child of card) */
        lv_obj_t *name = lv_obj_get_child(card, 0);
        if (name) {
            lv_obj_set_style_text_color(name,
                cur ? UI_COLOR_PRIMARY : UI_COLOR_TEXT, 0);
            lv_obj_set_style_text_font(name,
                cur ? &lv_font_montserrat_32 : &lv_font_montserrat_28, 0);
        }
        /* Show/hide the ✓ mark (second child, only on active card) */
        lv_obj_t *mark = lv_obj_get_child(card, 1);
        if (mark) {
            if (cur) lv_obj_clear_flag(mark, LV_OBJ_FLAG_HIDDEN);
            else     lv_obj_add_flag(mark,   LV_OBJ_FLAG_HIDDEN);
        }
    }
}

#undef SNAP_H_BASE
#undef SNAP_H_CUR

static void build_setlist_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    pedalboard_t *pb = ui_pedalboard_get();

    /* ── Left: pedalboard list (1/3) ── */
    lv_obj_t *left = lv_obj_create(parent);
    lv_obj_set_width(left, LV_PCT(33));
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_bg_color(left, lv_color_hex(0x181818), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_pad_all(left, 12, 0);
    lv_obj_set_style_pad_row(left, 8, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);

    mpt_settings_t *s = settings_get();
    bank_list_t *banks = calloc(1, sizeof(*banks));
    if (!banks) return;
    int bank_count = 0;
    if (bank_load(s->banks_file, banks) == 0)
        bank_count = banks->bank_count;

    /* Clamp g_bank_idx in case banks were removed since last save */
    if (g_bank_idx >= bank_count) g_bank_idx = -1;

    /* ── Bank selector button ── */
    const char *bank_btn_label = (g_bank_idx < 0)
        ? TR(TR_SCENE_ALL_BANKS)
        : (bank_count > 0 ? banks->banks[g_bank_idx].name : TR(TR_SCENE_ALL_BANKS));

    lv_obj_t *bank_btn = lv_btn_create(left);
    lv_obj_set_size(bank_btn, LV_PCT(100), 46);
    lv_obj_set_style_bg_color(bank_btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(bank_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(bank_btn, 1, 0);
    lv_obj_set_style_radius(bank_btn, 8, 0);
    lv_obj_set_style_shadow_width(bank_btn, 0, 0);
    lv_obj_set_style_pad_hor(bank_btn, 10, 0);
    lv_obj_t *bank_btn_lbl = lv_label_create(bank_btn);
    lv_label_set_text_fmt(bank_btn_lbl, LV_SYMBOL_DOWN "  %s", bank_btn_label);
    lv_obj_set_style_text_color(bank_btn_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(bank_btn_lbl, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(bank_btn_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(bank_btn_lbl, LV_PCT(90));
    lv_obj_align(bank_btn_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    /* bank_btn_cb needs the banks pointer — pass a heap copy as user_data */
    bank_list_t *banks_cb = malloc(sizeof(*banks_cb));
    if (banks_cb) {
        *banks_cb = *banks;
        lv_obj_add_event_cb(bank_btn, bank_btn_cb,        LV_EVENT_CLICKED, banks_cb);
        lv_obj_add_event_cb(bank_btn, bank_list_delete_cb, LV_EVENT_DELETE,  banks_cb);
    }

    /* ── Pedalboard list ── */
    lv_obj_t *pb_list = lv_obj_create(left);
    lv_obj_set_size(pb_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(pb_list, 530, 0);
    lv_obj_set_style_bg_opa(pb_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pb_list, 0, 0);
    lv_obj_set_style_pad_all(pb_list, 0, 0);
    lv_obj_set_style_pad_row(pb_list, 6, 0);
    lv_obj_set_flex_flow(pb_list, LV_FLEX_FLOW_COLUMN);

    if (bank_count > 0) {
        /* Determine which pedals to show */
        if (g_bank_idx < 0) {
            /* All banks: iterate every bank in order */
            for (int bi = 0; bi < bank_count; bi++) {
                bank_t *bank = &banks->banks[bi];
                for (int pi = 0; pi < bank->pedal_count; pi++) {
                    bank_pedal_t *pedal = &bank->pedals[pi];
                    bool is_cur = pb && strcmp(pedal->bundle, pb->path) == 0;
                    lv_obj_t *row = lv_btn_create(pb_list);
                    lv_obj_set_size(row, LV_PCT(100), 60);
                    lv_obj_set_style_bg_color(row,
                        is_cur ? lv_color_hex(0x2C2C2C) : lv_color_hex(0x1E1E1E), 0);
                    lv_obj_set_style_border_color(row,
                        is_cur ? UI_COLOR_PRIMARY : lv_color_hex(0x333333), 0);
                    lv_obj_set_style_border_width(row, is_cur ? 2 : 1, 0);
                    lv_obj_set_style_radius(row, 8, 0);
                    lv_obj_set_style_shadow_width(row, 0, 0);
                    char *bundle_copy = strdup(pedal->bundle);
                    lv_obj_add_event_cb(row, pb_list_item_cb,        LV_EVENT_CLICKED, bundle_copy);
                    lv_obj_add_event_cb(row, pb_list_item_delete_cb, LV_EVENT_DELETE,  bundle_copy);
                    lv_obj_t *lbl = lv_label_create(row);
                    lv_label_set_text(lbl, pedal->title);
                    lv_obj_set_style_text_color(lbl,
                        is_cur ? UI_COLOR_PRIMARY : UI_COLOR_TEXT, 0);
                    lv_obj_set_style_text_font(lbl,
                        is_cur ? &lv_font_montserrat_18 : &lv_font_montserrat_16, 0);
                    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
                    lv_obj_set_width(lbl, LV_PCT(90));
                    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
                }
            }
        } else {
            bank_t *bank = &banks->banks[g_bank_idx];
            for (int pi = 0; pi < bank->pedal_count; pi++) {
                bank_pedal_t *pedal = &bank->pedals[pi];
                bool is_cur = pb && strcmp(pedal->bundle, pb->path) == 0;
                lv_obj_t *row = lv_btn_create(pb_list);
                lv_obj_set_size(row, LV_PCT(100), 60);
                lv_obj_set_style_bg_color(row,
                    is_cur ? lv_color_hex(0x2C2C2C) : lv_color_hex(0x1E1E1E), 0);
                lv_obj_set_style_border_color(row,
                    is_cur ? UI_COLOR_PRIMARY : lv_color_hex(0x333333), 0);
                lv_obj_set_style_border_width(row, is_cur ? 2 : 1, 0);
                lv_obj_set_style_radius(row, 8, 0);
                lv_obj_set_style_shadow_width(row, 0, 0);
                char *bundle_copy = strdup(pedal->bundle);
                lv_obj_add_event_cb(row, pb_list_item_cb,        LV_EVENT_CLICKED, bundle_copy);
                lv_obj_add_event_cb(row, pb_list_item_delete_cb, LV_EVENT_DELETE,  bundle_copy);
                lv_obj_t *lbl = lv_label_create(row);
                lv_label_set_text(lbl, pedal->title);
                lv_obj_set_style_text_color(lbl,
                    is_cur ? UI_COLOR_PRIMARY : UI_COLOR_TEXT, 0);
                lv_obj_set_style_text_font(lbl,
                    is_cur ? &lv_font_montserrat_18 : &lv_font_montserrat_16, 0);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
                lv_obj_set_width(lbl, LV_PCT(90));
                lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
            }
        }
    }
    free(banks);

    /* Vertical separator */
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, 1, LV_PCT(100));
    lv_obj_set_style_bg_color(sep, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    /* ── Right: snapshot grid (2/3) ── */
    lv_obj_t *right = lv_obj_create(parent);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_style_bg_color(right, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_set_style_pad_all(right, 16, 0);
    lv_obj_set_style_pad_row(right, 12, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *right_title = lv_label_create(right);
    lv_label_set_text(right_title, "Snapshots");
    lv_obj_set_style_text_color(right_title, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(right_title, &lv_font_montserrat_14, 0);

    /* Base cell height; active cell is 30% taller */
    #define SNAP_H_BASE 100
    #define SNAP_H_CUR  ((SNAP_H_BASE * 13) / 10)   /* ×1.3 = 130 */

    lv_obj_t *snap_list = lv_obj_create(right);
    lv_obj_set_size(snap_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(snap_list, 590, 0);
    lv_obj_set_style_bg_opa(snap_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(snap_list, 0, 0);
    lv_obj_set_style_pad_all(snap_list, 0, 0);
    lv_obj_set_style_pad_row(snap_list, 8, 0);
    lv_obj_set_flex_flow(snap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(snap_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    if (!pb || pb->snapshot_count == 0) {
        lv_obj_t *empty = lv_label_create(snap_list);
        lv_label_set_text(empty, TR(TR_SCENE_NO_SNAPS));
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
    } else {
        for (int si = 0; si < pb->snapshot_count; si++) {
            bool is_cur = (si == pb->current_snapshot);
            int  card_h = is_cur ? SNAP_H_CUR : SNAP_H_BASE;

            lv_obj_t *card = lv_obj_create(snap_list);
            lv_obj_set_size(card, LV_PCT(100), card_h);
            lv_obj_set_style_bg_color(card,
                is_cur ? lv_color_hex(0x252525) : UI_COLOR_SURFACE, 0);
            lv_obj_set_style_border_color(card,
                is_cur ? UI_COLOR_PRIMARY : lv_color_hex(0x444444), 0);
            lv_obj_set_style_border_width(card, is_cur ? 2 : 1, 0);
            lv_obj_set_style_radius(card, 10, 0);
            lv_obj_set_style_pad_all(card, 10, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, snap_item_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)si);

            if (is_cur) {
                lv_obj_set_style_shadow_color(card, UI_COLOR_PRIMARY, 0);
                lv_obj_set_style_shadow_width(card, 22, 0);
                lv_obj_set_style_shadow_spread(card, 6, 0);
                lv_obj_set_style_shadow_opa(card, LV_OPA_60, 0);
                lv_obj_set_style_shadow_ofs_x(card, 0, 0);
                lv_obj_set_style_shadow_ofs_y(card, 0, 0);
            }

            lv_obj_t *name = lv_label_create(card);
            lv_label_set_text(name, pb->snapshots[si].name);
            lv_obj_set_style_text_color(name,
                is_cur ? UI_COLOR_PRIMARY : UI_COLOR_TEXT, 0);
            lv_obj_set_style_text_font(name,
                is_cur ? &lv_font_montserrat_32 : &lv_font_montserrat_28, 0);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_width(name, LV_PCT(100));
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

            if (is_cur) {
                lv_obj_t *mark = lv_label_create(card);
                lv_label_set_text(mark, LV_SYMBOL_OK);
                lv_obj_set_style_text_color(mark, UI_COLOR_PRIMARY, 0);
                lv_obj_set_style_text_font(mark, &lv_font_montserrat_14, 0);
                lv_obj_align(mark, LV_ALIGN_RIGHT_MID, -4, 0);
            }
        }
    }

    #undef SNAP_H_BASE
    #undef SNAP_H_CUR
}

/* ─── Tab switching ──────────────────────────────────────────────────────────── */

static void show_tab(int tab)
{
    g_active_tab = tab;
    if (g_content_pedals)  lv_obj_add_flag(g_content_pedals,  LV_OBJ_FLAG_HIDDEN);
    if (g_content_setlist) lv_obj_add_flag(g_content_setlist, LV_OBJ_FLAG_HIDDEN);

    if (tab == 0) {
        if (g_content_pedals)  lv_obj_clear_flag(g_content_pedals,  LV_OBJ_FLAG_HIDDEN);
        if (g_tab_btn_pedals)  lv_obj_set_style_bg_color(g_tab_btn_pedals,  UI_COLOR_PRIMARY,  0);
        if (g_tab_btn_setlist) lv_obj_set_style_bg_color(g_tab_btn_setlist, UI_COLOR_SURFACE,  0);
    } else {
        if (g_content_setlist) lv_obj_clear_flag(g_content_setlist, LV_OBJ_FLAG_HIDDEN);
        if (g_tab_btn_pedals)  lv_obj_set_style_bg_color(g_tab_btn_pedals,  UI_COLOR_SURFACE,  0);
        if (g_tab_btn_setlist) lv_obj_set_style_bg_color(g_tab_btn_setlist, UI_COLOR_PRIMARY,  0);
    }
}

static void tab_pedals_cb(lv_event_t *e)  { (void)e; show_tab(0); }
static void tab_setlist_cb(lv_event_t *e) { (void)e; show_tab(1); }

/* Rebuild both content panels in-place after a pedalboard change. */
static void scene_refresh(void)
{
    if (!g_overlay) return;

    hide_spinner();

    /* Cancel any ongoing MIDI learn */
    g_learning_slot = -1;

    /* Reload slot assignments from the new pedalboard's scene.json */
    scene_load();

    /* Null stale LVGL object pointers before cleaning the pedals tab */
    for (int i = 0; i < SLOT_COUNT; i++) {
        g_slots[i].card       = NULL;
        g_slots[i].name_lbl   = NULL;
        g_slots[i].state_lbl  = NULL;
        g_slots[i].cancel_btn = NULL;
    }

    lv_obj_clean(g_content_pedals);
    build_pedals_tab(g_content_pedals);

    lv_obj_clean(g_content_setlist);
    build_setlist_tab(g_content_setlist);

    /* Restore whichever tab was active */
    show_tab(g_active_tab);
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_scene_open(void)
{
    if (g_overlay) return;

    scene_load();
    scene_bank_load();
    g_learning_slot = -1;

    /* Full-screen overlay */
    g_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_overlay, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_set_style_pad_row(g_overlay, 0, 0);
    lv_obj_set_flex_flow(g_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Tab bar ── */
    lv_obj_t *tab_bar = lv_obj_create(g_overlay);
    lv_obj_set_size(tab_bar, LV_PCT(100), 60);
    lv_obj_set_style_bg_color(tab_bar, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_radius(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 6, 0);
    lv_obj_set_style_pad_column(tab_bar, 8, 0);
    lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);

#define TAB_BTN(obj, text, cb) do { \
    obj = lv_btn_create(tab_bar); \
    lv_obj_set_size(obj, 160, 46); \
    lv_obj_set_style_radius(obj, 8, 0); \
    lv_obj_set_style_shadow_width(obj, 0, 0); \
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL); \
    lv_obj_t *_l = lv_label_create(obj); \
    lv_label_set_text(_l, text); \
    lv_obj_set_style_text_color(_l, UI_COLOR_TEXT, 0); \
    lv_obj_set_style_text_font(_l, &lv_font_montserrat_16, 0); \
    lv_obj_center(_l); \
} while(0)

    TAB_BTN(g_tab_btn_pedals,  TR(TR_SCENE_PEDALS),  tab_pedals_cb);
    TAB_BTN(g_tab_btn_setlist, TR(TR_SCENE_SETLIST), tab_setlist_cb);

#undef TAB_BTN

    /* Spacer */
    lv_obj_t *spacer = lv_obj_create(tab_bar);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    /* ── Content area ── */
    lv_obj_t *content_wrap = lv_obj_create(g_overlay);
    lv_obj_set_size(content_wrap, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content_wrap, 1);
    lv_obj_set_style_bg_opa(content_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_wrap, 0, 0);
    lv_obj_set_style_pad_all(content_wrap, 0, 0);
    lv_obj_clear_flag(content_wrap, LV_OBJ_FLAG_SCROLLABLE);

    g_content_pedals = lv_obj_create(content_wrap);
    lv_obj_set_size(g_content_pedals, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(g_content_pedals, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_content_pedals, 0, 0);
    lv_obj_set_style_pad_all(g_content_pedals, 0, 0);
    build_pedals_tab(g_content_pedals);

    g_content_setlist = lv_obj_create(content_wrap);
    lv_obj_set_size(g_content_setlist, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(g_content_setlist, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_content_setlist, 0, 0);
    lv_obj_set_style_pad_all(g_content_setlist, 0, 0);
    lv_obj_add_flag(g_content_setlist, LV_OBJ_FLAG_HIDDEN);
    build_setlist_tab(g_content_setlist);

    /* ── Floating close button — top-right corner ── */
    lv_obj_t *btn_close = lv_btn_create(g_overlay);
    lv_obj_add_flag(btn_close, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(btn_close, 44, 44);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_80, 0);
    lv_obj_set_style_radius(btn_close, 22, 0);
    lv_obj_set_style_border_color(btn_close, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(btn_close, 1, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, scene_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(lbl_close, UI_COLOR_TEXT_DIM, 0);
    lv_obj_center(lbl_close);

    show_tab(0);
}

void ui_scene_close(void)
{
    if (!g_overlay) return;
    lv_obj_delete_async(g_overlay);
    g_overlay         = NULL;
    g_content_pedals  = NULL;
    g_content_setlist = NULL;
    g_tab_btn_pedals  = NULL;
    g_tab_btn_setlist = NULL;
    g_spinner         = NULL;
    g_bank_menu       = NULL;
    g_learning_slot   = -1;
    g_active_tab      = 0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        g_slots[i].card       = NULL;
        g_slots[i].name_lbl   = NULL;
        g_slots[i].state_lbl  = NULL;
        g_slots[i].cancel_btn = NULL;
    }
}

bool ui_scene_is_open(void) { return g_overlay != NULL; }

void ui_scene_on_midi_mapped(int instance_id, const char *symbol,
                              int ch, int cc, float min, float max)
{
    (void)ch; (void)cc; (void)min; (void)max;
    if (!g_overlay) return;
    if (strcmp(symbol, ":bypass") != 0) return;
    if (g_learning_slot < 0) return;
    if (g_slots[g_learning_slot].instance_id != instance_id) return;

    int learned = g_learning_slot;
    g_learning_slot = -1;
    slot_apply_style(learned);
}

void ui_scene_on_bypass_changed(int instance_id, bool enabled)
{
    (void)enabled;
    if (!g_overlay) return;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (g_slots[i].instance_id == instance_id)
            slot_apply_style(i);
}
