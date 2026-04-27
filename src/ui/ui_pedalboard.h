#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/* ── Block geometry (shared with ui_plugin_block) ────────────────────────────── */
#define LAYOUT_BLOCK_W  160
#define LAYOUT_BLOCK_H  160
#define LAYOUT_H_GAP     80
#define LAYOUT_V_GAP     40

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

/* Called after each plugin is added during pedalboard load.
 * done  = number of plugins loaded so far
 * total = total number of plugins in the bundle
 * Invoked from whichever thread calls ui_pedalboard_load(). */
typedef void (*pb_progress_cb_t)(int done, int total, void *ud);

/* Load a pedalboard bundle and display it.
 * progress_cb (may be NULL) is called after each plugin is added to mod-host. */
void ui_pedalboard_load(const char *bundle_path,
                        pb_progress_cb_t progress_cb, void *progress_ud);

/* Called by host feedback to update a port value indicator. */
void ui_pedalboard_update_param(int instance_id, const char *symbol, float value);

/* Called by ui_plugin_block on short click: returns true if the event was
 * consumed by an active connection mode (plugin block should skip its menu). */
bool ui_pedalboard_intercept_plugin_click(int instance_id);

/* Apply a snapshot by index to mod-host (also saves last state). */
void ui_pedalboard_apply_snapshot(int idx);

/* Persist current pedalboard path + snapshot index to last.json. */
void ui_pedalboard_save_last_state(void);

/* Bypass all pedalboard plugins (for tuner mute). When bypass_all=false,
 * each plugin is restored to its stored enabled state. */
void ui_pedalboard_chain_bypass(bool bypass_all);

/* Called from the LVGL thread when mod-host confirms a MIDI CC assignment.
 * Updates pedalboard data and the open param editor (if any). */
void ui_pedalboard_on_midi_mapped(int instance_id, const char *symbol,
                                  int ch, int cc, float min, float max);

/* Enable or disable the Virtual MIDI Loopback for the current pedalboard.
 * Updates pb->midi_loopback, issues the mod-host command, and marks modified. */
void ui_pedalboard_set_midi_loopback(bool enabled);

/* Accessors */
pedalboard_t *ui_pedalboard_get(void);
bool          ui_pedalboard_is_loaded(void);

/* Output port value table — updated by the feedback thread via monitor_output.
 * set_output: thread-safe; notifies the param editor via lv_async_call if open.
 * get_output: returns true and fills *out if a value has been received. */
void ui_pedalboard_set_output(int instance, const char *symbol, float value);
bool ui_pedalboard_get_output(int instance, const char *symbol, float *out);

/* CV output port enable/disable.
 * When a port is disabled all cv_map assignments that used it are removed. */
bool ui_pedalboard_is_cv_out_enabled(int instance_id, const char *symbol);
void ui_pedalboard_set_cv_out_enabled(int instance_id, const char *symbol, bool enabled);
