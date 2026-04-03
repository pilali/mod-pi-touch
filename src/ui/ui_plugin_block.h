#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/*
 * A plugin block widget: shows name, bypass toggle, port indicators.
 * Supports tap (open param editor), long-press menu (remove, bypass).
 */

lv_obj_t *ui_plugin_block_create(lv_obj_t *parent, pb_plugin_t *plug,
                                  lv_event_cb_t on_tap,
                                  lv_event_cb_t on_bypass,
                                  lv_event_cb_t on_remove,
                                  void *userdata);

/* Update the visual bypass state of an existing block. */
void ui_plugin_block_set_bypassed(lv_obj_t *block, bool bypassed);
