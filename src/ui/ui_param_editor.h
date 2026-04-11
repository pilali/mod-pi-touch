#pragma once
#include <lvgl.h>
#include "../pedalboard.h"
#include "../plugin_manager.h"

/* Show a parameter editor modal for a plugin.
 * plugin_uri   — LV2 URI, used to look up port metadata from plugin_manager.
 * enabled      — current bypass state (true = active, false = bypassed).
 * bypass_cb    — called when the in-editor bypass button is tapped.
 * value_cb     — called when a parameter value changes. */
typedef void (*param_change_cb_t)(int instance_id, const char *symbol,
                                  float value, void *userdata);
typedef void (*patch_change_cb_t)(int instance_id, const char *param_uri,
                                  const char *path, void *userdata);
typedef void (*bypass_toggle_cb_t)(void *userdata);

void ui_param_editor_show(int instance_id,
                          const char *plugin_label,
                          const char *plugin_uri,
                          pb_port_t *ports, int port_count,
                          pb_patch_t *patch_params, int patch_param_count,
                          bool enabled,
                          bypass_toggle_cb_t bypass_cb, void *bypass_ud,
                          param_change_cb_t value_cb, void *value_ud,
                          patch_change_cb_t patch_cb, void *patch_ud);

void ui_param_editor_close(void);

/* Update a displayed value from host feedback */
void ui_param_editor_update(const char *symbol, float value);
