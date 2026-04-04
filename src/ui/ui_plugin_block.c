#include "ui_plugin_block.h"
#include "ui_app.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK_W 160
#define BLOCK_H  80

typedef struct {
    block_cb_t on_tap;
    block_cb_t on_bypass;
    block_cb_t on_remove;
    void      *userdata;
    bool       bypassed;
    bool       long_pressed; /* suppress the CLICKED that follows LONG_PRESSED */
} block_ctx_t;

typedef struct {
    block_ctx_t *bc;
    lv_obj_t    *menu;
} menu_ctx_t;

static void menu_bypass_cb(lv_event_t *e)
{
    menu_ctx_t *mc = lv_event_get_user_data(e);
    lv_obj_del(mc->menu);
    if (mc->bc->on_bypass) mc->bc->on_bypass(mc->bc->userdata);
    free(mc);
}

static void menu_remove_cb(lv_event_t *e)
{
    menu_ctx_t *mc = lv_event_get_user_data(e);
    lv_obj_del(mc->menu);
    if (mc->bc->on_remove) mc->bc->on_remove(mc->bc->userdata);
    free(mc);
}

static void block_tap_cb(lv_event_t *e)
{
    block_ctx_t *ctx = lv_event_get_user_data(e);
    ctx->long_pressed = true; /* flag so the following CLICKED is ignored */
    if (ctx->on_tap) ctx->on_tap(ctx->userdata);
}

static void overlay_tap_close_cb(lv_event_t *e)
{
    /* Close if the tap landed directly on the overlay (outside the menu box) */
    lv_obj_t *overlay = lv_event_get_current_target(e);
    lv_obj_t *target  = lv_event_get_target(e);
    if (target == overlay)
        lv_obj_del(overlay);
}

static void block_long_press_cb(lv_event_t *e)
{
    block_ctx_t *ctx = lv_event_get_user_data(e);

    /* Suppress the CLICKED that LVGL fires after a long press release */
    if (ctx->long_pressed) {
        ctx->long_pressed = false;
        return;
    }

    /* Full-screen overlay — tap outside the box to dismiss */
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, overlay_tap_close_cb, LV_EVENT_CLICKED, NULL);

    /* Centered menu box */
    lv_obj_t *menu = lv_obj_create(overlay);
    lv_obj_set_size(menu, 200, 110);
    lv_obj_center(menu);
    lv_obj_set_style_bg_color(menu, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(menu, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(menu, 2, 0);
    lv_obj_set_style_radius(menu, 10, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(menu, 8, 0);
    lv_obj_set_style_pad_row(menu, 6, 0);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

    /* mc->menu points to the overlay so both buttons close the whole thing */
    menu_ctx_t *mc = malloc(sizeof(*mc));
    mc->bc = ctx; mc->menu = overlay;

    lv_obj_t *btn_bypass = lv_btn_create(menu);
    lv_obj_set_size(btn_bypass, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(btn_bypass, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_bypass = lv_label_create(btn_bypass);
    lv_label_set_text(lbl_bypass, ctx->bypassed ? "Enable" : "Bypass");
    lv_obj_center(lbl_bypass);
    lv_obj_add_event_cb(btn_bypass, menu_bypass_cb, LV_EVENT_CLICKED, mc);

    lv_obj_t *btn_remove = lv_btn_create(menu);
    lv_obj_set_size(btn_remove, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(btn_remove, lv_color_hex(0xCC2222), 0);
    lv_obj_t *lbl_remove = lv_label_create(btn_remove);
    lv_label_set_text(lbl_remove, "Remove");
    lv_obj_center(lbl_remove);
    lv_obj_add_event_cb(btn_remove, menu_remove_cb, LV_EVENT_CLICKED, mc);
}

lv_obj_t *ui_plugin_block_create(lv_obj_t *parent, pb_plugin_t *plug,
                                  block_cb_t on_tap,
                                  block_cb_t on_bypass,
                                  block_cb_t on_remove,
                                  void *userdata)
{
    block_ctx_t *ctx = malloc(sizeof(*ctx));
    ctx->on_tap    = on_tap;
    ctx->on_bypass = on_bypass;
    ctx->on_remove = on_remove;
    ctx->userdata  = userdata;
    ctx->bypassed  = !plug->enabled;

    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, BLOCK_W, BLOCK_H);
    lv_obj_set_style_bg_color(block, plug->enabled ? UI_COLOR_SURFACE : UI_COLOR_BYPASS, 0);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(block, plug->enabled ? UI_COLOR_PRIMARY : UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(block, 2, 0);
    lv_obj_set_style_radius(block, 8, 0);
    lv_obj_set_style_pad_all(block, 6, 0);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(block, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(block);
    lv_label_set_text(lbl, plug->label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, BLOCK_W - 12);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *dot = lv_label_create(block);
    lv_label_set_text(dot, plug->enabled ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(dot, plug->enabled ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* Short tap  → small menu (bypass/remove)
     * Long press → open parameter editor */
    lv_obj_add_event_cb(block, block_long_press_cb, LV_EVENT_CLICKED,      ctx);
    lv_obj_add_event_cb(block, block_tap_cb,        LV_EVENT_LONG_PRESSED, ctx);

    return block;
}

void ui_plugin_block_set_bypassed(lv_obj_t *block, bool bypassed)
{
    lv_obj_set_style_bg_color(block, bypassed ? UI_COLOR_BYPASS : UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(block,
        bypassed ? UI_COLOR_TEXT_DIM : UI_COLOR_PRIMARY, 0);
}
