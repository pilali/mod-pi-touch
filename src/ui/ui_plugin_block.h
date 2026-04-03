#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/*
 * A plugin block widget: shows name, bypass toggle, port indicators.
 * Supports tap (open param editor), long-press menu (remove, bypass).
 *
 * Callbacks receive the original userdata passed to ui_plugin_block_create.
 * Using a custom callback type avoids reliance on lv_event_set_user_data
 * which was removed in LVGL 9.x.
 */
typedef void (*block_cb_t)(void *userdata);

lv_obj_t *ui_plugin_block_create(lv_obj_t *parent, pb_plugin_t *plug,
                                  block_cb_t on_tap,
                                  block_cb_t on_bypass,
                                  block_cb_t on_remove,
                                  void *userdata);

/* Update the visual bypass state of an existing block. */
void ui_plugin_block_set_bypassed(lv_obj_t *block, bool bypassed);
