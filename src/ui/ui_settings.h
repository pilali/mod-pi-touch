#pragma once
#include <lvgl.h>

/* Show the settings panel inside parent. */
void ui_settings_show(lv_obj_t *parent);

/* Must be called before lv_obj_clean() destroys the settings widgets.
 * Cancels the wifi poll timer and NULLs all widget pointers so that
 * background threads posting lv_async_call cannot write to freed objects. */
void ui_settings_before_hide(void);
