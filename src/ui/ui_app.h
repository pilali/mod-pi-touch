#pragma once

#include <lvgl.h>

/*
 * ui_app — Application root layout and navigation for mod-pi-touch
 *
 * Screen layout (1280×720):
 * ┌────────────────────────────────────────────────────────┐
 * │                   Top bar (60px)                       │
 * ├────────────────────────────────────────────────────────┤
 * │                                                        │
 * │              Main content area (580px)                 │
 * │              (pedalboard canvas)                       │
 * │                                                        │
 * ├────────────────────────────────────────────────────────┤
 * │               Snapshot bar (80px)                      │
 * └────────────────────────────────────────────────────────┘
 */

#define UI_TOP_BAR_H      60
#define UI_SNAPSHOT_BAR_H 80
#define UI_CANVAS_H       (720 - UI_TOP_BAR_H - UI_SNAPSHOT_BAR_H)

/* App screen identifiers */
typedef enum {
    UI_SCREEN_PEDALBOARD = 0,
    UI_SCREEN_PLUGIN_BROWSER,
    UI_SCREEN_BANK_BROWSER,
    UI_SCREEN_SETTINGS,
} ui_screen_t;

/* Initialize the application UI (call after lv_init()) */
void ui_app_init(void);

/* Switch to a different screen */
void ui_app_show_screen(ui_screen_t screen);

/* Update top bar: pedalboard name, modified indicator */
void ui_app_update_title(const char *name, bool modified);

/* Show a modal message (auto-dismiss after ms=0 means user must tap OK) */
void ui_app_show_message(const char *title, const char *body, int autodismiss_ms);

/* Show a text input modal. callback(text, userdata) called on confirm. */
typedef void (*ui_input_cb_t)(const char *text, void *userdata);
void ui_app_show_input(const char *title, const char *placeholder,
                       ui_input_cb_t cb, void *userdata);

/* Show a confirm dialog. ok_cb called if user confirms. */
typedef void (*ui_confirm_cb_t)(void *userdata);
void ui_app_show_confirm(const char *title, const char *message,
                         ui_confirm_cb_t ok_cb, void *userdata);

/* Returns the main content area parent object */
lv_obj_t *ui_app_content_area(void);

/* Global theme colors */
extern lv_color_t UI_COLOR_BG;
extern lv_color_t UI_COLOR_SURFACE;
extern lv_color_t UI_COLOR_PRIMARY;
extern lv_color_t UI_COLOR_ACCENT;
extern lv_color_t UI_COLOR_TEXT;
extern lv_color_t UI_COLOR_TEXT_DIM;
extern lv_color_t UI_COLOR_BYPASS;
extern lv_color_t UI_COLOR_ACTIVE;
