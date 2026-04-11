#pragma once
#include <lvgl.h>

/* Callback invoked when the user selects a file.
 * full_path is the absolute file path, userdata is passed through.
 * Called with full_path == NULL if the user cancels. */
typedef void (*file_browser_cb_t)(const char *full_path, void *userdata);

/* Open a file browser modal.
 * start_dir   — initial directory (NULL → "/")
 * title       — dialog title
 * exts        — NULL-terminated array of lowercase extensions to show,
 *               e.g. {".nam", ".json", NULL}. NULL = show all files.
 * cb          — selection callback
 * userdata    — passed to cb */
void ui_file_browser_open(const char *start_dir,
                          const char *title,
                          const char **exts,
                          file_browser_cb_t cb,
                          void *userdata);

void ui_file_browser_close(void);
