#pragma once

#include "pedalboard.h"

/*
 * snapshot — read/write snapshots.json (mod-ui compatible format)
 *
 * Format:
 * {
 *   "current": 0,
 *   "snapshots": [
 *     {
 *       "name": "Clean",
 *       "data": {
 *         "<plugin_symbol>": {
 *           "bypassed": false,
 *           "parameters": { "<symbol>": <value>, ... },
 *           "ports": { "<symbol>": <value>, ... },
 *           "preset": "<uri>"
 *         }
 *       }
 *     }
 *   ]
 * }
 */

/* Load snapshots from a JSON file into the snapshots array.
 * current_out receives the "current" index from the file.
 * Returns number of snapshots loaded, or -1 on error. */
int snapshot_load(const char *path, pb_snapshot_t *snaps, int *count_out,
                  int max_snaps, int *current_out);

/* Save snapshots array to a JSON file.
 * Returns 0 on success. */
int snapshot_save(const char *path, const pb_snapshot_t *snaps, int count,
                  int current);
