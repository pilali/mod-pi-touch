#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* ─── Load ───────────────────────────────────────────────────────────────────── */

int snapshot_load(const char *path, pb_snapshot_t *snaps, int *count_out,
                  int max_snaps, int *current_out)
{
    *count_out   = 0;
    *current_out = -1;

    FILE *f = fopen(path, "r");
    if (!f) return 0; /* not an error — new pedalboard has no snapshots */

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len <= 0) { fclose(f); return -1; }
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    cJSON *cur = cJSON_GetObjectItem(root, "current");
    if (cJSON_IsNumber(cur)) *current_out = (int)cur->valuedouble;

    cJSON *snaps_arr = cJSON_GetObjectItem(root, "snapshots");
    if (!cJSON_IsArray(snaps_arr)) { cJSON_Delete(root); return 0; }

    int n = cJSON_GetArraySize(snaps_arr);
    if (n > max_snaps) n = max_snaps;

    for (int si = 0; si < n; si++) {
        cJSON *snap_obj = cJSON_GetArrayItem(snaps_arr, si);
        if (!cJSON_IsObject(snap_obj)) continue;

        pb_snapshot_t *snap = &snaps[*count_out];
        memset(snap, 0, sizeof(*snap));

        cJSON *name = cJSON_GetObjectItem(snap_obj, "name");
        if (cJSON_IsString(name))
            snprintf(snap->name, sizeof(snap->name), "%s", name->valuestring);

        cJSON *data = cJSON_GetObjectItem(snap_obj, "data");
        if (!cJSON_IsObject(data)) { (*count_out)++; continue; }

        /* Each key in "data" is a plugin symbol */
        cJSON *plugin_entry = data->child;
        while (plugin_entry && snap->plugin_count < PB_MAX_PLUGINS) {
            snap_plugin_t *sp = &snap->plugins[snap->plugin_count];
            memset(sp, 0, sizeof(*sp));
            snprintf(sp->symbol, sizeof(sp->symbol), "%s", plugin_entry->string);

            cJSON *bypassed = cJSON_GetObjectItem(plugin_entry, "bypassed");
            if (cJSON_IsBool(bypassed)) sp->bypassed = cJSON_IsTrue(bypassed);

            cJSON *preset = cJSON_GetObjectItem(plugin_entry, "preset");
            if (cJSON_IsString(preset) && preset->valuestring[0])
                snprintf(sp->preset_uri, sizeof(sp->preset_uri), "%s", preset->valuestring);

            /* "ports" → regular control params (numbers) */
            cJSON *ports = cJSON_GetObjectItem(plugin_entry, "ports");
            if (cJSON_IsObject(ports)) {
                cJSON *p = ports->child;
                while (p && sp->param_count < PB_MAX_PORTS) {
                    if (cJSON_IsNumber(p)) {
                        snap_param_t *param = &sp->params[sp->param_count++];
                        snprintf(param->symbol, sizeof(param->symbol), "%s", p->string);
                        param->value = (float)p->valuedouble;
                    }
                    p = p->next;
                }
            }

            /* "parameters" → patch:writable params, stored as [path, type_char] arrays */
            cJSON *parameters = cJSON_GetObjectItem(plugin_entry, "parameters");
            if (cJSON_IsObject(parameters)) {
                cJSON *p = parameters->child;
                while (p && sp->patch_param_count < SNAP_MAX_PATCH_PARAMS) {
                    /* Each value is [path_string, type_char] */
                    if (cJSON_IsArray(p) && cJSON_GetArraySize(p) >= 2) {
                        cJSON *val = cJSON_GetArrayItem(p, 0);
                        cJSON *typ = cJSON_GetArrayItem(p, 1);
                        if (cJSON_IsString(val) && cJSON_IsString(typ)) {
                            /* Only handle path ("p") and string ("s") types */
                            const char *tc = typ->valuestring;
                            if (tc[0] == 'p' || tc[0] == 's') {
                                snap_patch_t *pp = &sp->patch_params[sp->patch_param_count++];
                                snprintf(pp->uri,  sizeof(pp->uri),  "%s", p->string);
                                snprintf(pp->path, sizeof(pp->path), "%s", val->valuestring);
                            }
                        }
                    }
                    p = p->next;
                }
            }

            snap->plugin_count++;
            plugin_entry = plugin_entry->next;
        }

        (*count_out)++;
    }

    cJSON_Delete(root);
    return *count_out;
}

/* ─── Save ───────────────────────────────────────────────────────────────────── */

int snapshot_save(const char *path, const pb_snapshot_t *snaps, int count,
                  int current)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "current", current);

    cJSON *snaps_arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "snapshots", snaps_arr);

    for (int si = 0; si < count; si++) {
        const pb_snapshot_t *snap = &snaps[si];
        cJSON *snap_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(snap_obj, "name", snap->name);

        cJSON *data = cJSON_CreateObject();
        cJSON_AddItemToObject(snap_obj, "data", data);

        for (int pi = 0; pi < snap->plugin_count; pi++) {
            const snap_plugin_t *sp = &snap->plugins[pi];
            cJSON *plugin_obj = cJSON_CreateObject();
            cJSON_AddBoolToObject(plugin_obj, "bypassed", sp->bypassed);

            /* "parameters" → patch:writable params as [path, "p"] arrays */
            cJSON *params_obj = cJSON_CreateObject();
            for (int i = 0; i < sp->patch_param_count; i++) {
                cJSON *arr = cJSON_CreateArray();
                cJSON_AddItemToArray(arr, cJSON_CreateString(sp->patch_params[i].path));
                cJSON_AddItemToArray(arr, cJSON_CreateString("p"));
                cJSON_AddItemToObject(params_obj, sp->patch_params[i].uri, arr);
            }
            cJSON_AddItemToObject(plugin_obj, "parameters", params_obj);

            /* "ports" → regular control params as numbers */
            cJSON *ports_obj = cJSON_CreateObject();
            for (int i = 0; i < sp->param_count; i++)
                cJSON_AddNumberToObject(ports_obj, sp->params[i].symbol,
                                        (double)sp->params[i].value);
            cJSON_AddItemToObject(plugin_obj, "ports", ports_obj);

            cJSON_AddStringToObject(plugin_obj, "preset",
                                    sp->preset_uri[0] ? sp->preset_uri : "");

            cJSON_AddItemToObject(data, sp->symbol, plugin_obj);
        }

        cJSON_AddItemToArray(snaps_arr, snap_obj);
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
