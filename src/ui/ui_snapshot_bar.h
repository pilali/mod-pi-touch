#pragma once
#include <lvgl.h>

/* Initialize the snapshot bar inside parent. */
void ui_snapshot_bar_init(lv_obj_t *parent);

/* Refresh the snapshot bar from current pedalboard snapshot list. */
void ui_snapshot_bar_refresh(void);
