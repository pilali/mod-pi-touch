#pragma once
#include <lvgl.h>
#include "../pedalboard.h"
#include "../plugin_manager.h"

/* Show a parameter editor modal for a plugin.
 * plugin_uri   — LV2 URI, used to look up port metadata from plugin_manager.
 * enabled      — current bypass state (true = active, false = bypassed).
 * bypass_cb    — called when the in-editor bypass button is tapped.
 * value_cb     — called when a parameter value changes.
 * midi_cb      — called when a MIDI mapping changes (assign or unmap).
 *                symbol=":bypass" for bypass mapping. */
typedef void (*param_change_cb_t)(int instance_id, const char *symbol,
                                  float value, void *userdata);
typedef void (*patch_change_cb_t)(int instance_id, const char *param_uri,
                                  const char *path, void *userdata);
typedef void (*bypass_toggle_cb_t)(void *userdata);
typedef void (*midi_map_cb_t)(int instance_id, const char *symbol,
                              int midi_ch, int midi_cc,
                              float min, float max, void *userdata);

/* Called when user assigns or removes a CV assignment.
 * cv_uri == "" means unmap. */
typedef void (*cv_map_cb_t)(int instance_id, const char *symbol,
                            const char *cv_uri, const char *jack_port,
                            float min, float max, char op_mode,
                            void *userdata);

void ui_param_editor_show(int instance_id,
                          const char *plugin_label,
                          const char *plugin_uri,
                          pb_port_t *ports, int port_count,
                          pb_patch_t *patch_params, int patch_param_count,
                          bool enabled,
                          bypass_toggle_cb_t bypass_cb, void *bypass_ud,
                          param_change_cb_t value_cb, void *value_ud,
                          patch_change_cb_t patch_cb, void *patch_ud,
                          midi_map_cb_t midi_cb, void *midi_ud,
                          pb_cv_source_t *cv_sources, int cv_source_count,
                          cv_map_cb_t cv_cb, void *cv_ud,
                          bypass_toggle_cb_t remove_cb, void *remove_ud);

void ui_param_editor_close(void);

/* Update a displayed value from host feedback */
void ui_param_editor_update(const char *symbol, float value);

/* Update a read-only output port meter (called via lv_async_call from
 * ui_pedalboard_set_output). No-op if the editor is closed or symbol unknown. */
void ui_param_editor_update_output(const char *symbol, float value);

/* Returns the instance_id currently open in the editor, or -1 if closed. */
int ui_param_editor_instance(void);

/* Called from the LVGL thread when mod-host confirms a MIDI learn assignment.
 * Updates the MIDI chip label for the matching symbol.
 * ch/cc == -1 signals unmap confirmation. */
void ui_param_editor_on_midi_mapped(int instance_id, const char *symbol,
                                    int ch, int cc, float min, float max);
