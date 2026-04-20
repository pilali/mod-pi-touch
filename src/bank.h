#pragma once

#include <stddef.h>

#define BANK_NAME_MAX     256
#define BANK_PATH_MAX     1024
#define BANK_MAX_PEDALS   64
#define BANK_MAX          128

typedef struct {
    char title[BANK_NAME_MAX];
    char bundle[BANK_PATH_MAX]; /* pedalboard bundle path */
} bank_pedal_t;

typedef struct {
    char        name[BANK_NAME_MAX];
    bank_pedal_t pedals[BANK_MAX_PEDALS];
    int          pedal_count;
} bank_t;

typedef struct {
    bank_t banks[BANK_MAX];
    int    bank_count;
} bank_list_t;

/* Load banks.json. Returns 0 on success. */
int bank_load(const char *path, bank_list_t *list);

/* Save banks.json. Returns 0 on success. */
int bank_save(const char *path, const bank_list_t *list);

/* Add a pedalboard to a bank (creates bank if needed). */
int bank_add_pedal(bank_list_t *list, const char *bank_name,
                   const char *pedal_title, const char *bundle_path);

/* Remove a pedalboard entry. */
void bank_remove_pedal(bank_list_t *list, int bank_idx, int pedal_idx);

/* Create a new empty bank. Returns bank index, or -1 on error. */
int bank_create(bank_list_t *list, const char *name);

/* Delete a bank (does not affect pedalboard files). */
void bank_delete(bank_list_t *list, int bank_idx);

/* Move a pedal within a bank (reorder). */
void bank_move_pedal(bank_list_t *list, int bank_idx, int from_idx, int to_idx);

/* Update all bundle paths that match old_path to new_path (for pedalboard rename). */
void bank_update_bundle_path(bank_list_t *list, const char *old_path, const char *new_path);
