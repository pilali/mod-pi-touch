#include "ui_plugin_block.h"
#include "ui_pedalboard.h"
#include "ui_app.h"
#include "../i18n.h"
#include "../plugin_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define BLOCK_W 160
#define BLOCK_H 160

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
    lv_obj_add_flag(mc->menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(mc->menu);
    block_ctx_t *bc = mc->bc;
    free(mc);
    if (bc->on_bypass) bc->on_bypass(bc->userdata);
}

static void menu_remove_cb(lv_event_t *e)
{
    menu_ctx_t *mc = lv_event_get_user_data(e);
    lv_obj_add_flag(mc->menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(mc->menu);
    block_ctx_t *bc = mc->bc;
    free(mc);
    if (bc->on_remove) bc->on_remove(bc->userdata);
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
    if (target == overlay) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(overlay);
    }
}

static void block_long_press_cb(lv_event_t *e)
{
    block_ctx_t *ctx = lv_event_get_user_data(e);

    /* Suppress the CLICKED that LVGL fires after a long press release */
    if (ctx->long_pressed) {
        ctx->long_pressed = false;
        return;
    }

    /* In connecting mode a short tap selects the target plugin */
    if (ui_pedalboard_intercept_plugin_click((int)(intptr_t)ctx->userdata))
        return;

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
    lv_label_set_text(lbl_bypass, ctx->bypassed ? TR(TR_PLUG_ENABLE) : TR(TR_PLUG_BYPASS));
    lv_obj_center(lbl_bypass);
    lv_obj_add_event_cb(btn_bypass, menu_bypass_cb, LV_EVENT_CLICKED, mc);

    lv_obj_t *btn_remove = lv_btn_create(menu);
    lv_obj_set_size(btn_remove, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(btn_remove, UI_COLOR_DANGER, 0);
    lv_obj_t *lbl_remove = lv_label_create(btn_remove);
    lv_label_set_text(lbl_remove, TR(TR_PLUG_REMOVE));
    lv_obj_center(lbl_remove);
    lv_obj_add_event_cb(btn_remove, menu_remove_cb, LV_EVENT_CLICKED, mc);
}

/* Read PNG width/height from the IHDR chunk (26-byte read, no full decode). */
static bool png_get_size(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t buf[26];
    bool ok = fread(buf, 1, sizeof(buf), f) == sizeof(buf);
    fclose(f);
    if (!ok) return false;
    /* PNG: 8-byte sig, then IHDR: 4 len + 4 type + 4 width + 4 height */
    *w = (int)((buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19]);
    *h = (int)((buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23]);
    return *w > 0 && *h > 0;
}

static void img_src_free_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
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
    lv_obj_set_style_bg_color(block, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_grad_color(block, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_grad_dir(block, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(block, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(block, 4, 0);
    lv_obj_set_style_border_color(block, plug->enabled ? UI_COLOR_PRIMARY : UI_COLOR_BYPASS, 0);
    lv_obj_set_style_radius(block, 12, 0);
    lv_obj_set_style_shadow_color(block, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_width(block, plug->enabled ? 20 : 0, 0);
    lv_obj_set_style_shadow_spread(block, 3, 0);
    lv_obj_set_style_shadow_opa(block, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_x(block, 0, 0);
    lv_obj_set_style_shadow_ofs_y(block, 0, 0);
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

    /* Thumbnail — centered in the area below the label */
    const pm_plugin_info_t *pi = pm_plugin_by_uri(plug->uri);
    if (pi && pi->thumbnail_path[0]) {
        int img_w = 0, img_h = 0;
        if (png_get_size(pi->thumbnail_path, &img_w, &img_h)) {
            int avail_w = BLOCK_W - 12;
            int avail_h = BLOCK_H - 12 - 18; /* below label */
            float sw = (float)avail_w / img_w;
            float sh = (float)avail_h / img_h;
            float scale = sw < sh ? sw : sh;
            if (scale > 3.0f) scale = 3.0f;
            int lv_scale = (int)(scale * 256 + 0.5f);

            /* "A:<absolute_path>" — LVGL POSIX FS driver with letter 'A' */
            char *src = malloc(strlen(pi->thumbnail_path) + 3);
            if (src) {
                src[0] = 'A'; src[1] = ':'; src[2] = '\0';
                strcat(src, pi->thumbnail_path);

                lv_obj_t *img = lv_image_create(block);
                lv_image_set_src(img, src);
                lv_image_set_scale(img, (uint32_t)lv_scale);
                lv_obj_align(img, LV_ALIGN_BOTTOM_MID, 0, 0);
                lv_obj_add_event_cb(block, img_src_free_cb, LV_EVENT_DELETE, src);
            }
        }
    }

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
    lv_obj_set_style_border_color(block,
        bypassed ? UI_COLOR_BYPASS : UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_width(block, bypassed ? 0 : 20, 0);
    lv_obj_set_style_shadow_color(block, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_spread(block, 3, 0);
    lv_obj_set_style_shadow_opa(block, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_x(block, 0, 0);
    lv_obj_set_style_shadow_ofs_y(block, 0, 0);
}
