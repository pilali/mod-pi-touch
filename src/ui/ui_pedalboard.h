#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/* Initialize the pedalboard canvas view inside parent. */
void ui_pedalboard_init(lv_obj_t *parent);

/* Reload the canvas from the current pedalboard state. */
void ui_pedalboard_refresh(void);

/* Trigger a save of the current pedalboard (TTL + snapshots.json). */
void ui_pedalboard_save(void);

/* Overwrite the active snapshot slot with current live state, save snapshots.json. */
void ui_pedalboard_save_snapshot(void);

/* Delete the current snapshot from the pedalboard, save snapshots.json. */
void ui_pedalboard_delete_snapshot(void);

/* Delete the current pedalboard bundle from disk and unload it. */
void ui_pedalboard_delete(void);

/* Load a pedalboard bundle and display it. */
void ui_pedalboard_load(const char *bundle_path);

/* Called by host feedback to update a port value indicator. */
void ui_pedalboard_update_param(int instance_id, const char *symbol, float value);

/* Called by ui_plugin_block on short click: returns true if the event was
 * consumed by an active connection mode (plugin block should skip its menu). */
bool ui_pedalboard_intercept_plugin_click(int instance_id);

/* Apply a snapshot by index to mod-host (also saves last state). */
void ui_pedalboard_apply_snapshot(int idx);

/* Persist current pedalboard path + snapshot index to last.json. */
void ui_pedalboard_save_last_state(void);

/* Accessors */
pedalboard_t *ui_pedalboard_get(void);
bool          ui_pedalboard_is_loaded(void);
