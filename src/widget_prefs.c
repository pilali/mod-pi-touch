#include "widget_prefs.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/* ── Runtime storage ─────────────────────────────────────────────────────────── */

typedef struct {
    char plug_symbol[PB_SYMBOL_MAX];
    char syms[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX];
    int  count;
} wp_entry_t;

static wp_entry_t g_entries[PB_MAX_PLUGINS];
static int        g_entry_count = 0;

/* ── Path helper ─────────────────────────────────────────────────────────────── */

static void build_path(const char *data_dir, const char *pb_path,
                       char *out, size_t out_sz)
{
    const char *slash = strrchr(pb_path, '/');
    const char *base  = slash ? slash + 1 : pb_path;
    char name[256];
    snprintf(name, sizeof(name), "%s", base);
    /* Strip .pedalboard extension if present */
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    snprintf(out, out_sz, "%s/widget_controls/%s.json", data_dir, name);
}

/* ── Public API ──────────────────────────────────────────────────────────────── */

void widget_prefs_clear(void)
{
    g_entry_count = 0;
}

int widget_prefs_get(const char *plug_symbol,
                     char syms_out[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX])
{
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].plug_symbol, plug_symbol) == 0) {
            if (g_entries[i].count > 0) {
                memcpy(syms_out, g_entries[i].syms,
                       WIDGET_PREFS_MAX_CTRL * PB_SYMBOL_MAX);
                return g_entries[i].count;
            }
            return 0;
        }
    }
    return 0;
}

void widget_prefs_set(const char *plug_symbol,
                      const char syms[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX],
                      int count)
{
    /* Find existing entry */
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].plug_symbol, plug_symbol) == 0) {
            if (count == 0) {
                /* Remove: compact array */
                g_entries[i] = g_entries[--g_entry_count];
            } else {
                g_entries[i].count = count;
                memcpy(g_entries[i].syms, syms, WIDGET_PREFS_MAX_CTRL * PB_SYMBOL_MAX);
            }
            return;
        }
    }
    if (count == 0 || g_entry_count >= PB_MAX_PLUGINS) return;
    wp_entry_t *e = &g_entries[g_entry_count++];
    snprintf(e->plug_symbol, PB_SYMBOL_MAX, "%s", plug_symbol);
    e->count = count;
    memcpy(e->syms, syms, WIDGET_PREFS_MAX_CTRL * PB_SYMBOL_MAX);
}

void widget_prefs_load(const char *data_dir, const char *pb_path)
{
    widget_prefs_clear();

    char path[1024];
    build_path(data_dir, pb_path, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;

    cJSON *plugins = cJSON_GetObjectItem(root, "plugins");
    if (!plugins || !cJSON_IsObject(plugins)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, plugins) {
        const char *sym = item->string;
        if (!sym || !cJSON_IsArray(item)) continue;
        char syms[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX];
        int count = 0;
        cJSON *s = NULL;
        cJSON_ArrayForEach(s, item) {
            if (count >= WIDGET_PREFS_MAX_CTRL) break;
            if (!cJSON_IsString(s) || !s->valuestring) continue;
            snprintf(syms[count], PB_SYMBOL_MAX, "%s", s->valuestring);
            count++;
        }
        if (count > 0)
            widget_prefs_set(sym, syms, count);
    }
    cJSON_Delete(root);
}

void widget_prefs_save(const char *data_dir, const char *pb_path)
{
    /* Ensure directory exists */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/widget_controls", data_dir);
    mkdir(dir, 0755);

    char path[1024];
    build_path(data_dir, pb_path, path, sizeof(path));

    cJSON *root = cJSON_CreateObject();
    cJSON *plugins = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "plugins", plugins);

    for (int i = 0; i < g_entry_count; i++) {
        wp_entry_t *e = &g_entries[i];
        if (e->count <= 0) continue;
        cJSON *arr = cJSON_CreateArray();
        for (int k = 0; k < e->count; k++)
            cJSON_AddItemToArray(arr, cJSON_CreateString(e->syms[k]));
        cJSON_AddItemToObject(plugins, e->plug_symbol, arr);
    }

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!str) return;

    FILE *f = fopen(path, "w");
    if (f) {
        fputs(str, f);
        fclose(f);
    }
    free(str);
}
