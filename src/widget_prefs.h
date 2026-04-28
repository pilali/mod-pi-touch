#pragma once
#include "pedalboard.h"  /* PB_SYMBOL_MAX, PB_MAX_PLUGINS */

#define WIDGET_PREFS_MAX_CTRL 8

/* Load custom widget control selections for the current pedalboard.
 * data_dir: mod-pi-touch data dir (e.g. "/home/pi/.mod-pi-touch")
 * pb_path:  pedalboard bundle dir (e.g. "/home/pi/.pedalboards/MyPB.pedalboard") */
void widget_prefs_load(const char *data_dir, const char *pb_path);

/* Save current selections to the sidecar file. */
void widget_prefs_save(const char *data_dir, const char *pb_path);

/* Clear all entries (call on pedalboard unload or new). */
void widget_prefs_clear(void);

/* Get custom symbol list for a plugin (by its TTL symbol, i.e. pb_plugin_t.symbol).
 * Fills syms_out and returns count (0..WIDGET_PREFS_MAX_CTRL).
 * Returns 0 if no custom selection — caller should use defaults. */
int widget_prefs_get(const char *plug_symbol,
                     char syms_out[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX]);

/* Set custom symbol list. count=0 removes the entry (revert to defaults). */
void widget_prefs_set(const char *plug_symbol,
                      const char syms[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX],
                      int count);
