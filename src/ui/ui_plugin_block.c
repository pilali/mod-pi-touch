#include "ui_plugin_block.h"
#include "ui_app.h"

#include <string.h>
#include <stdio.h>

#define BLOCK_W 160
#define BLOCK_H  80

typedef struct {
    lv_event_cb_t on_tap;
    lv_event_cb_t on_bypass;
    lv_event_cb_t on_remove;
    void         *userdata;
    lv_obj_t     *bypass_dot;
    bool          bypassed;
} block_ctx_t;

static void block_tap_cb(lv_event_t *e)
{
    block_ctx_t *ctx = lv_event_get_user_data(e);
    /* Forward to the caller's tap handler with original userdata */
    lv_event_set_user_data(e, ctx->userdata);
    if (ctx->on_tap) ctx->on_tap(e);
}

static void block_long_press_cb(lv_event_t *e)
{
    block_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *block  = lv_event_get_target(e);

    /* Simple context menu as msgbox */
    lv_obj_t *menu = lv_obj_create(lv_layer_top());
    lv_obj_set_size(menu, 180, 100);
    lv_obj_set_pos(menu, lv_obj_get_x(block), lv_obj_get_y(block));
    lv_obj_set_style_bg_color(menu, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(menu, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(menu, 1, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(menu, 4, 0);
    lv_obj_set_style_gap(menu, 4, 0);

    /* Bypass toggle */
    lv_obj_t *btn_bypass = lv_btn_create(menu);
    lv_obj_set_width(btn_bypass, LV_PCT(100));
    lv_obj_set_height(btn_bypass, 36);
    lv_obj_set_style_bg_color(btn_bypass, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_bypass = lv_label_create(btn_bypass);
    lv_label_set_text(lbl_bypass, ctx->bypassed ? "Enable" : "Bypass");
    lv_obj_center(lbl_bypass);

    /* Remove */
    lv_obj_t *btn_remove = lv_btn_create(menu);
    lv_obj_set_width(btn_remove, LV_PCT(100));
    lv_obj_set_height(btn_remove, 36);
    lv_obj_set_style_bg_color(btn_remove, lv_color_hex(0xCC2222), 0);
    lv_obj_t *lbl_remove = lv_label_create(btn_remove);
    lv_label_set_text(lbl_remove, "Remove");
    lv_obj_center(lbl_remove);

    /* Context for menu buttons */
    typedef struct { block_ctx_t *bc; lv_obj_t *menu; } menu_ctx_t;
    menu_ctx_t *mc = malloc(sizeof(*mc));
    mc->bc = ctx; mc->menu = menu;

    lv_obj_add_event_cb(btn_bypass, menu_bypass_cb, LV_EVENT_CLICKED, mc);
}

/* Helper callbacks for the context menu (avoiding lambdas) */
typedef struct { block_ctx_t *bc; lv_obj_t *menu; } menu_ctx_t;

static void menu_bypass_cb(lv_event_t *e)
{
    menu_ctx_t *mc = lv_event_get_user_data(e);
    lv_obj_del(mc->menu);
    lv_event_set_user_data(e, mc->bc->userdata);
    if (mc->bc->on_bypass) mc->bc->on_bypass(e);
    free(mc);
}

static void menu_remove_cb(lv_event_t *e)
{
    menu_ctx_t *mc = lv_event_get_user_data(e);
    lv_obj_del(mc->menu);
    lv_event_set_user_data(e, mc->bc->userdata);
    if (mc->bc->on_remove) mc->bc->on_remove(e);
    free(mc);
}

static void block_long_press_cb_v2(lv_event_t *e)
{
    block_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *block  = lv_event_get_target(e);

    lv_obj_t *menu = lv_obj_create(lv_layer_top());
    lv_obj_set_size(menu, 180, 100);
    lv_obj_set_pos(menu, lv_obj_get_x(block) + 160, lv_obj_get_y(block));
    lv_obj_set_style_bg_color(menu, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(menu, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(menu, 1, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(menu, 4, 0);
    lv_obj_set_style_gap(menu, 4, 0);

    menu_ctx_t *mc = malloc(sizeof(*mc));
    mc->bc = ctx; mc->menu = menu;

    lv_obj_t *btn_bypass = lv_btn_create(menu);
    lv_obj_set_width(btn_bypass, LV_PCT(100));
    lv_obj_set_height(btn_bypass, 36);
    lv_obj_set_style_bg_color(btn_bypass, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_bypass = lv_label_create(btn_bypass);
    lv_label_set_text(lbl_bypass, ctx->bypassed ? "Enable" : "Bypass");
    lv_obj_center(lbl_bypass);
    lv_obj_add_event_cb(btn_bypass, menu_bypass_cb, LV_EVENT_CLICKED, mc);

    lv_obj_t *btn_remove = lv_btn_create(menu);
    lv_obj_set_width(btn_remove, LV_PCT(100));
    lv_obj_set_height(btn_remove, 36);
    lv_obj_set_style_bg_color(btn_remove, lv_color_hex(0xCC2222), 0);
    lv_obj_t *lbl_remove = lv_label_create(btn_remove);
    lv_label_set_text(lbl_remove, "Remove");
    lv_obj_center(lbl_remove);
    lv_obj_add_event_cb(btn_remove, menu_remove_cb, LV_EVENT_CLICKED, mc);
}

lv_obj_t *ui_plugin_block_create(lv_obj_t *parent, pb_plugin_t *plug,
                                  lv_event_cb_t on_tap,
                                  lv_event_cb_t on_bypass,
                                  lv_event_cb_t on_remove,
                                  void *userdata)
{
    block_ctx_t *ctx = malloc(sizeof(*ctx));
    ctx->on_tap    = on_tap;
    ctx->on_bypass = on_bypass;
    ctx->on_remove = on_remove;
    ctx->userdata  = userdata;
    ctx->bypassed  = !plug->enabled;

    /* Block container */
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

    /* Plugin name */
    lv_obj_t *lbl = lv_label_create(block);
    lv_label_set_text(lbl, plug->label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, BLOCK_W - 12);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Bypass indicator dot */
    ctx->bypass_dot = lv_label_create(block);
    lv_label_set_text(ctx->bypass_dot, plug->enabled ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ctx->bypass_dot,
        plug->enabled ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
    lv_obj_align(ctx->bypass_dot, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* Events */
    lv_obj_add_event_cb(block, block_tap_cb,           LV_EVENT_CLICKED,     ctx);
    lv_obj_add_event_cb(block, block_long_press_cb_v2, LV_EVENT_LONG_PRESSED, ctx);

    return block;
}

void ui_plugin_block_set_bypassed(lv_obj_t *block, bool bypassed)
{
    lv_obj_set_style_bg_color(block, bypassed ? UI_COLOR_BYPASS : UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(block,
        bypassed ? UI_COLOR_TEXT_DIM : UI_COLOR_PRIMARY, 0);
}
