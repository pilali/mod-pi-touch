#pragma once
#include <lvgl.h>
#include "../pedalboard.h"

/* Show a parameter editor modal for a plugin.
 * value_cb is called when a value changes. */
typedef void (*param_change_cb_t)(int instance_id, const char *symbol,
                                  float value, void *userdata);

void ui_param_editor_show(int instance_id, const char *plugin_label,
                          pb_port_t *ports, int port_count,
                          param_change_cb_t value_cb, void *userdata);

void ui_param_editor_close(void);

/* Update a displayed value from feedback */
void ui_param_editor_update(const char *symbol, float value);
