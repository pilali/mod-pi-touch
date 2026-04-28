#pragma once
#include <stdbool.h>

void ui_scene_open(void);
void ui_scene_close(void);
bool ui_scene_is_open(void);

/* Called from the feedback dispatcher in main.c when mod-host confirms a
 * MIDI CC assignment.  Refreshes any slot currently in learning mode. */
void ui_scene_on_midi_mapped(int instance_id, const char *symbol,
                              int ch, int cc, float min, float max);

/* Called when bypass state changes externally (e.g. snapshot applied).
 * Refreshes the glow on the matching slot if the scene is open. */
void ui_scene_on_bypass_changed(int instance_id, bool enabled);
