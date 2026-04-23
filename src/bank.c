#include "bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "lv2_utils.h"


int bank_load(const char *path, bank_list_t *list)
{
    memset(list, 0, sizeof(*list));

    FILE *f = fopen(path, "r");
    if (!f) return 0; /* Empty bank list is valid */

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    /* mod-ui banks.json format:
     * [ { "name": "...", "pedalboards": [ { "title": "...", "bundle": "..." } ] } ] */
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return -1; }

    int nb = cJSON_GetArraySize(root);
    if (nb > BANK_MAX) nb = BANK_MAX;

    for (int bi = 0; bi < nb; bi++) {
        cJSON *bank_obj = cJSON_GetArrayItem(root, bi);
        if (!cJSON_IsObject(bank_obj)) continue;

        bank_t *bank = &list->banks[list->bank_count];
        memset(bank, 0, sizeof(*bank));

        /* mod-ui banks.json uses "title" at bank level; older format used "name" */
        cJSON *name = cJSON_GetObjectItem(bank_obj, "title");
        if (!cJSON_IsString(name)) name = cJSON_GetObjectItem(bank_obj, "name");
        if (cJSON_IsString(name)) {
            snprintf(bank->name, sizeof(bank->name), "%s", name->valuestring);
            lv2u_normalize_quotes(bank->name);
        }

        cJSON *pedals = cJSON_GetObjectItem(bank_obj, "pedalboards");
        if (!cJSON_IsArray(pedals)) { list->bank_count++; continue; }

        int np = cJSON_GetArraySize(pedals);
        if (np > BANK_MAX_PEDALS) np = BANK_MAX_PEDALS;
        for (int pi = 0; pi < np; pi++) {
            cJSON *p = cJSON_GetArrayItem(pedals, pi);
            if (!cJSON_IsObject(p)) continue;
            bank_pedal_t *pedal = &bank->pedals[bank->pedal_count];
            cJSON *title  = cJSON_GetObjectItem(p, "title");
            cJSON *bundle = cJSON_GetObjectItem(p, "bundle");
            if (cJSON_IsString(title)) {
                snprintf(pedal->title, sizeof(pedal->title), "%s", title->valuestring);
                lv2u_normalize_quotes(pedal->title);
            }
            if (cJSON_IsString(bundle)) snprintf(pedal->bundle, sizeof(pedal->bundle), "%s", bundle->valuestring);
            bank->pedal_count++;
        }
        list->bank_count++;
    }

    cJSON_Delete(root);
    return 0;
}

int bank_save(const char *path, const bank_list_t *list)
{
    cJSON *root = cJSON_CreateArray();

    for (int bi = 0; bi < list->bank_count; bi++) {
        const bank_t *bank = &list->banks[bi];
        cJSON *bank_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(bank_obj, "title", bank->name);

        cJSON *pedals = cJSON_CreateArray();
        for (int pi = 0; pi < bank->pedal_count; pi++) {
            const bank_pedal_t *p = &bank->pedals[pi];
            cJSON *pobj = cJSON_CreateObject();
            cJSON_AddStringToObject(pobj, "title",  p->title);
            cJSON_AddStringToObject(pobj, "bundle", p->bundle);
            cJSON_AddItemToArray(pedals, pobj);
        }
        cJSON_AddItemToObject(bank_obj, "pedalboards", pedals);
        cJSON_AddItemToArray(root, bank_obj);
    }

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!str) return -1;

    FILE *f = fopen(path, "w");
    if (!f) { free(str); return -1; }
    fputs(str, f);
    fclose(f);
    free(str);
    return 0;
}

int bank_add_pedal(bank_list_t *list, const char *bank_name,
                   const char *pedal_title, const char *bundle_path)
{
    /* Find or create bank */
    bank_t *bank = NULL;
    for (int i = 0; i < list->bank_count; i++) {
        if (strcmp(list->banks[i].name, bank_name) == 0) {
            bank = &list->banks[i];
            break;
        }
    }
    if (!bank) {
        if (list->bank_count >= BANK_MAX) return -1;
        bank = &list->banks[list->bank_count++];
        memset(bank, 0, sizeof(*bank));
        snprintf(bank->name, sizeof(bank->name), "%s", bank_name);
    }

    if (bank->pedal_count >= BANK_MAX_PEDALS) return -1;
    bank_pedal_t *p = &bank->pedals[bank->pedal_count];
    snprintf(p->title,  sizeof(p->title),  "%s", pedal_title);
    snprintf(p->bundle, sizeof(p->bundle), "%s", bundle_path);
    bank->pedal_count++;
    return 0;
}

void bank_remove_pedal(bank_list_t *list, int bank_idx, int pedal_idx)
{
    if (bank_idx < 0 || bank_idx >= list->bank_count) return;
    bank_t *bank = &list->banks[bank_idx];
    if (pedal_idx < 0 || pedal_idx >= bank->pedal_count) return;
    memmove(&bank->pedals[pedal_idx], &bank->pedals[pedal_idx+1],
            (bank->pedal_count - pedal_idx - 1) * sizeof(bank_pedal_t));
    bank->pedal_count--;
}

int bank_create(bank_list_t *list, const char *name)
{
    if (list->bank_count >= BANK_MAX) return -1;
    bank_t *bank = &list->banks[list->bank_count];
    memset(bank, 0, sizeof(*bank));
    snprintf(bank->name, sizeof(bank->name), "%s", name);
    return list->bank_count++;
}

void bank_delete(bank_list_t *list, int bank_idx)
{
    if (bank_idx < 0 || bank_idx >= list->bank_count) return;
    memmove(&list->banks[bank_idx], &list->banks[bank_idx + 1],
            (list->bank_count - bank_idx - 1) * sizeof(bank_t));
    list->bank_count--;
}

void bank_update_bundle_path(bank_list_t *list, const char *old_path, const char *new_path)
{
    for (int bi = 0; bi < list->bank_count; bi++) {
        bank_t *bank = &list->banks[bi];
        for (int pi = 0; pi < bank->pedal_count; pi++) {
            if (strcmp(bank->pedals[pi].bundle, old_path) == 0)
                snprintf(bank->pedals[pi].bundle, sizeof(bank->pedals[pi].bundle),
                         "%s", new_path);
        }
    }
}

void bank_move_pedal(bank_list_t *list, int bank_idx, int from_idx, int to_idx)
{
    if (bank_idx < 0 || bank_idx >= list->bank_count) return;
    bank_t *bank = &list->banks[bank_idx];
    if (from_idx == to_idx) return;
    if (from_idx < 0 || from_idx >= bank->pedal_count) return;
    if (to_idx   < 0 || to_idx   >= bank->pedal_count) return;
    bank_pedal_t tmp = bank->pedals[from_idx];
    if (from_idx < to_idx)
        memmove(&bank->pedals[from_idx], &bank->pedals[from_idx + 1],
                (to_idx - from_idx) * sizeof(bank_pedal_t));
    else
        memmove(&bank->pedals[to_idx + 1], &bank->pedals[to_idx],
                (from_idx - to_idx) * sizeof(bank_pedal_t));
    bank->pedals[to_idx] = tmp;
}
