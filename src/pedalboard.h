#pragma once

#include <stdbool.h>
#include <stddef.h>

/* ─── Constants ──────────────────────────────────────────────────────────────── */
#define PB_MAX_PLUGINS    256
#define PB_MAX_CONNECTS   512
#define PB_MAX_PORTS      64
#define PB_MAX_SNAPSHOTS  32
#define PB_URI_MAX        512
#define PB_NAME_MAX       256
#define PB_PATH_MAX       1024
#define PB_SYMBOL_MAX     128

/* ─── Patch parameter value (patch:writable atom:Path) ──────────────────────── */
#define PB_MAX_PATCH_PARAMS 8
typedef struct {
    char uri[PB_URI_MAX];       /* parameter URI */
    char path[PB_PATH_MAX];     /* current file path (empty = not set) */
} pb_patch_t;

/* ─── Plugin port (control input) ───────────────────────────────────────────── */
typedef struct {
    char   symbol[PB_SYMBOL_MAX];
    float  value;
    float  min;
    float  max;
    bool   snapshotable;
    int    midi_channel;   /* -1 = none */
    int    midi_cc;        /* -1 = none */
    float  midi_min;
    float  midi_max;
} pb_port_t;

/* ─── Plugin instance ────────────────────────────────────────────────────────── */
typedef struct {
    int    instance_id;         /* mod-host instance number */
    char   symbol[PB_SYMBOL_MAX]; /* short name for TTL */
    char   uri[PB_URI_MAX];
    char   label[PB_NAME_MAX];
    float  canvas_x;
    float  canvas_y;
    bool   enabled;             /* false = bypassed */
    bool   enabled_snapshotable;
    bool   preset_snapshotable;
    char   preset_uri[PB_URI_MAX];
    int    lv2_minor_version;
    int    lv2_micro_version;

    pb_port_t ports[PB_MAX_PORTS];
    int        port_count;

    pb_patch_t patch_params[PB_MAX_PATCH_PARAMS];
    int        patch_param_count;

    int    bypass_midi_channel;
    int    bypass_midi_cc;
} pb_plugin_t;

/* ─── Port connection ─────────────────────────────────────────────────────────── */
typedef struct {
    char from[PB_URI_MAX]; /* e.g. "effect_0:Out L" */
    char to[PB_URI_MAX];
} pb_connection_t;

/* ─── Snapshot entry ──────────────────────────────────────────────────────────── */
typedef struct {
    char symbol[PB_SYMBOL_MAX];
    float value;
} snap_param_t;

#define SNAP_MAX_PATCH_PARAMS 2
typedef struct {
    char uri[256];
    char path[512];
} snap_patch_t;

typedef struct {
    char symbol[PB_SYMBOL_MAX];
    bool  bypassed;
    char  preset_uri[PB_URI_MAX];
    snap_param_t  params[PB_MAX_PORTS];
    int           param_count;
    snap_patch_t  patch_params[SNAP_MAX_PATCH_PARAMS];
    int           patch_param_count;
} snap_plugin_t;

typedef struct {
    char        name[PB_NAME_MAX];
    snap_plugin_t plugins[PB_MAX_PLUGINS];
    int           plugin_count;
} pb_snapshot_t;

/* ─── Pedalboard ─────────────────────────────────────────────────────────────── */
typedef struct {
    char path[PB_PATH_MAX];   /* directory path (without trailing /) */
    char name[PB_NAME_MAX];

    pb_plugin_t    plugins[PB_MAX_PLUGINS];
    int             plugin_count;

    pb_connection_t connections[PB_MAX_CONNECTS];
    int              connection_count;

    pb_snapshot_t  snapshots[PB_MAX_SNAPSHOTS];
    int             snapshot_count;
    int             current_snapshot;

    float  bpm;
    float  bpb;
    bool   transport_rolling;   /* true = playing */
    int    transport_sync;      /* 0=internal, 1=midi_clock_slave */
    bool   modified;
} pedalboard_t;

/* ─── API ────────────────────────────────────────────────────────────────────── */

/* Initialize empty pedalboard */
void pb_init(pedalboard_t *pb);

/* Load a pedalboard bundle directory.
 * Reads manifest.ttl + <name>.ttl + snapshots.json.
 * Returns 0 on success. */
int pb_load(pedalboard_t *pb, const char *bundle_dir);

/* Save the pedalboard to its bundle directory (pb->path).
 * Creates directory if it does not exist.
 * Returns 0 on success. */
int pb_save(pedalboard_t *pb);

/* Save to a different location (also updates pb->path). */
int pb_save_as(pedalboard_t *pb, const char *new_dir);

/* List all pedalboard bundles under a directory.
 * Returns count; fills paths array (each entry is a malloc'd string). */
int pb_list(const char *base_dir, char **paths, int max_paths);

/* Read the doap:name of a pedalboard bundle without loading the full TTL.
 * Falls back to the filename stem on failure. Returns 0 on success. */
int pb_read_name(const char *bundle_path, char *out, size_t out_size);

/* Delete all files inside a bundle directory and the directory itself.
 * Returns 0 on success, -1 on error. */
int pb_bundle_delete(const char *bundle_path);

/* Find a plugin by instance_id. Returns NULL if not found. */
pb_plugin_t *pb_find_plugin(pedalboard_t *pb, int instance_id);

/* Find a plugin by symbol. Returns NULL if not found. */
pb_plugin_t *pb_find_plugin_by_symbol(pedalboard_t *pb, const char *symbol);

/* Add/remove plugin (does not communicate with mod-host). */
pb_plugin_t *pb_add_plugin(pedalboard_t *pb, int instance_id,
                            const char *symbol, const char *uri);
void pb_remove_plugin(pedalboard_t *pb, int instance_id);

/* Add/remove connection. */
int  pb_add_connection(pedalboard_t *pb, const char *from, const char *to);
void pb_remove_connection(pedalboard_t *pb, const char *from, const char *to);

/* Snapshot helpers */
int  pb_snapshot_save_current(pedalboard_t *pb, const char *name);
int  pb_snapshot_overwrite(pedalboard_t *pb, int index);   /* update slot with live state */
int  pb_snapshot_load(pedalboard_t *pb, int index);
void pb_snapshot_delete(pedalboard_t *pb, int index);
void pb_snapshot_rename(pedalboard_t *pb, int index, const char *name);
void pb_snapshot_move(pedalboard_t *pb, int from_idx, int to_idx);
