#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/*
 * A plugin block widget: shows name, bypass state, port indicators.
 * Long press opens the parameter editor; short press is free for widget interaction.
 *
 * Callbacks receive the original userdata passed to ui_plugin_block_create.
 * Using a custom callback type avoids reliance on lv_event_set_user_data
 * which was removed in LVGL 9.x.
 */
typedef void (*block_cb_t)(void *userdata);

/* Called when a control param changes directly on the block widget.
 * The callback must call host_param_set and update the pedalboard model. */
typedef void (*block_param_cb_t)(void *userdata, const char *symbol, float value);

lv_obj_t *ui_plugin_block_create(lv_obj_t *parent, pb_plugin_t *plug,
                                  block_cb_t on_tap,
                                  block_cb_t on_bypass,
                                  block_cb_t on_remove,
                                  block_param_cb_t on_param,
                                  void *userdata);

/* Update the visual bypass state of an existing block. */
void ui_plugin_block_set_bypassed(lv_obj_t *block, bool bypassed);

/* Update one control cell on an existing block (visual only, no host call). */
void ui_plugin_block_set_param(lv_obj_t *block, const char *symbol, float value);

/* Natural pixel width/height of the block for the given plugin.
 * Width depends on the number of modgui-curated (or fallback control) ports. */
int ui_plugin_block_width(const pb_plugin_t *plug);
int ui_plugin_block_height(const pb_plugin_t *plug);

/* Accent colour for the given LV2 category string (e.g. "Reverb", "Delay").
 * Returns UI_COLOR_PRIMARY for unknown categories. */
lv_color_t ui_plugin_block_category_color(const char *category);
