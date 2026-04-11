#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PM_URI_MAX    512
#define PM_NAME_MAX   256
#define PM_CAT_MAX    128
#define PM_PORT_MAX   64
#define PM_PATCH_MAX  8   /* max patch:writable parameters per plugin */

/* ─── Port descriptor (for display purposes) ─────────────────────────────────── */
typedef enum {
    PM_PORT_AUDIO_IN,
    PM_PORT_AUDIO_OUT,
    PM_PORT_MIDI_IN,
    PM_PORT_MIDI_OUT,
    PM_PORT_CONTROL_IN,
    PM_PORT_CONTROL_OUT,
    PM_PORT_CV_IN,
    PM_PORT_CV_OUT,
} pm_port_type_t;

typedef struct {
    char           symbol[PM_NAME_MAX];
    char           name[PM_NAME_MAX];
    pm_port_type_t type;
    float          default_val;
    float          min;
    float          max;
    bool           toggled;
    bool           integer;
    bool           enumeration;
    /* enum values (if enumeration) */
    char           enum_labels[16][PM_NAME_MAX];
    float          enum_values[16];
    int            enum_count;
} pm_port_info_t;

/* ─── Patch parameter (patch:writable with atom:Path range) ──────────────────── */
typedef struct {
    char uri[PM_URI_MAX];               /* full parameter URI */
    char label[PM_NAME_MAX];            /* rdfs:label */
    char file_types[PM_NAME_MAX];       /* mod:fileTypes e.g. "aidadspmodel" */
    char default_dir[PM_URI_MAX];       /* suggested browse root (bundle models/) */
} pm_patch_param_t;

/* ─── Plugin descriptor ───────────────────────────────────────────────────────── */
typedef struct {
    char uri[PM_URI_MAX];
    char name[PM_NAME_MAX];
    char author[PM_NAME_MAX];
    char category[PM_CAT_MAX];
    char subcategory[PM_CAT_MAX];

    pm_port_info_t ports[PM_PORT_MAX];
    int            port_count;

    int audio_in_count;
    int audio_out_count;
    int midi_in_count;
    int midi_out_count;
    int ctrl_in_count;

    pm_patch_param_t patch_params[PM_PATCH_MAX];
    int              patch_param_count;

    char thumbnail_path[PM_URI_MAX]; /* absolute fs path to modgui thumbnail PNG, or "" */
} pm_plugin_info_t;

/* ─── API ─────────────────────────────────────────────────────────────────────── */

/* Initialize the plugin manager and scan LV2 paths.
 * lv2_paths: colon-separated list of directories (NULL = system default).
 * cache_path: JSON cache file path (NULL = no caching).
 * This call may take several seconds on first run. */
int pm_init(const char *lv2_paths, const char *cache_path);

/* Release all resources. */
void pm_fini(void);

/* Number of discovered plugins. */
int pm_plugin_count(void);

/* Get plugin info by index (0-based). Returns NULL if out of range. */
const pm_plugin_info_t *pm_plugin_at(int index);

/* Find plugin by URI. Returns NULL if not found. */
const pm_plugin_info_t *pm_plugin_by_uri(const char *uri);

/* Get a flat sorted list of category names.
 * categories must be at least max_cats * PM_CAT_MAX bytes.
 * Returns number of unique categories. */
int pm_categories(char (*categories)[PM_CAT_MAX], int max_cats);

/* Get plugins in a category. indices filled with plugin indices.
 * Returns count. */
int pm_plugins_in_category(const char *category, int *indices, int max);

/* Search plugins by name substring (case-insensitive).
 * indices filled with plugin indices. Returns count. */
int pm_search(const char *query, int *indices, int max);
