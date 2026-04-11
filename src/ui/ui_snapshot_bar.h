#pragma once
#include <lvgl.h>

/* Initialize the snapshot bar inside parent. */
void ui_snapshot_bar_init(lv_obj_t *parent);

/* Refresh the snapshot bar from current pedalboard snapshot list. */
void ui_snapshot_bar_refresh(void);

/* Close any open popup/overlay (safe to call at any time). */
void ui_snapshot_bar_dismiss(void);

/* Update static label text after a language change. */
void ui_snapshot_bar_update_lang(void);
