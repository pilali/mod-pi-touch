#pragma once
#include <lvgl.h>
#include <stdbool.h>

/* ── Splash / boot screen ────────────────────────────────────────────────────
 * Shown on top of everything via lv_layer_top() during startup.
 * All functions must be called from the LVGL main thread, except
 * ui_splash_update() which marshals via lv_async_call and is safe from
 * any thread.
 * ──────────────────────────────────────────────────────────────────────────── */

/* Create and display the splash overlay (0 % progress). */
void ui_splash_show(void);

/* Update progress bar (0–100) and status message.
 * Thread-safe: may be called from any thread. */
void ui_splash_update(int pct, const char *msg);

/* Dismiss the splash with a short fade-out animation.
 * Must be called from the LVGL main thread. */
void ui_splash_hide(void);

/* Thread-safe: schedules ui_splash_hide() via lv_async_call. */
void ui_splash_hide_async(void);

/* Show a full-screen "shutting down / rebooting" screen and flush it to the
 * framebuffer synchronously.  Call immediately before exec(poweroff/reboot).
 * type: 0 = shutdown, 1 = reboot. */
void ui_splash_show_power(int type);
