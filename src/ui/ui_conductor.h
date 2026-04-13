#pragma once

#include <lvgl.h>
#include <stdbool.h>

/*
 * ui_conductor — Modal conductor/transport dialog.
 *
 * Opens a full-screen modal overlay on lv_layer_top() allowing the user to:
 *   - Set BPM (20–280) with +/− buttons and tap tempo
 *   - Set BPB (1–16, time signature numerator)
 *   - Choose clock source: Internal or MIDI Slave
 *   - Start / Stop transport rolling
 *
 * Changes are sent to mod-host immediately and stored in the pedalboard.
 */

/* Open the conductor dialog (no-op if already open). */
void ui_conductor_open(void);

/* Close the conductor dialog programmatically (e.g. on screen change). */
void ui_conductor_close(void);

/* Refresh displayed values from the current pedalboard (call after load). */
void ui_conductor_refresh(void);
