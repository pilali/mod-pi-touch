#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/* Initialize the pedalboard canvas view inside parent. */
void ui_pedalboard_init(lv_obj_t *parent);

/* Reload the canvas from the current pedalboard state. */
void ui_pedalboard_refresh(void);

/* Trigger a save of the current pedalboard (called from top bar). */
void ui_pedalboard_save(void);

/* Load a pedalboard bundle and display it. */
void ui_pedalboard_load(const char *bundle_path);

/* Called by host feedback to update a port value indicator. */
void ui_pedalboard_update_param(int instance_id, const char *symbol, float value);
