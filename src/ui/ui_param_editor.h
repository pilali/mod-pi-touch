#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/* Show a parameter editor modal for a plugin.
 * enabled      — current bypass state (true = active, false = bypassed).
 * bypass_cb    — called when the in-editor bypass button is tapped.
 * value_cb     — called when a parameter value changes. */
typedef void (*param_change_cb_t)(int instance_id, const char *symbol,
                                  float value, void *userdata);
typedef void (*bypass_toggle_cb_t)(void *userdata);

void ui_param_editor_show(int instance_id, const char *plugin_label,
                          pb_port_t *ports, int port_count,
                          bool enabled,
                          bypass_toggle_cb_t bypass_cb, void *bypass_ud,
                          param_change_cb_t value_cb, void *userdata);

void ui_param_editor_close(void);

/* Update a displayed value from feedback */
void ui_param_editor_update(const char *symbol, float value);
