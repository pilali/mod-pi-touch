#include "plugin_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <lilv/lilv.h>
#include "cJSON.h"

/* ─── Internal state ─────────────────────────────────────────────────────────── */

static LilvWorld      *g_world   = NULL;
static pm_plugin_info_t *g_plugins = NULL;
static int              g_count   = 0;

/* ─── Lilv helpers ───────────────────────────────────────────────────────────── */

static const char *lilv_str(const LilvNode *n)
{
    if (!n) return "";
    return lilv_node_as_string(n);
}

static void extract_port(LilvPlugin *plugin, LilvPort *port, pm_port_info_t *pi)
{
    LilvWorld *w = g_world;

    /* Symbol */
    LilvNode *sym_n = lilv_port_get_symbol(plugin, port);
    snprintf(pi->symbol, sizeof(pi->symbol), "%s", lilv_str(sym_n));

    /* Name */
    LilvNode *name_n = lilv_port_get_name(plugin, port);
    snprintf(pi->name, sizeof(pi->name), "%s", lilv_str(name_n));
    lilv_node_free(name_n);

    /* Type */
    LilvNode *audio  = lilv_new_uri(w, LV2_CORE__AudioPort);
    LilvNode *midi   = lilv_new_uri(w, LV2_CORE__MidiPort);
    LilvNode *ctrl   = lilv_new_uri(w, LV2_CORE__ControlPort);
    LilvNode *input  = lilv_new_uri(w, LV2_CORE__InputPort);
    LilvNode *output = lilv_new_uri(w, LV2_CORE__OutputPort);
    LilvNode *cv     = lilv_new_uri(w, LV2_CORE__CVPort);

    bool is_in  = lilv_port_is_a(plugin, port, input);
    bool is_out = lilv_port_is_a(plugin, port, output);

    if      (lilv_port_is_a(plugin, port, audio))
        pi->type = is_in ? PM_PORT_AUDIO_IN : PM_PORT_AUDIO_OUT;
    else if (lilv_port_is_a(plugin, port, midi))
        pi->type = is_in ? PM_PORT_MIDI_IN : PM_PORT_MIDI_OUT;
    else if (lilv_port_is_a(plugin, port, cv))
        pi->type = is_in ? PM_PORT_CV_IN : PM_PORT_CV_OUT;
    else
        pi->type = is_in ? PM_PORT_CONTROL_IN : PM_PORT_CONTROL_OUT;

    lilv_node_free(audio); lilv_node_free(midi); lilv_node_free(ctrl);
    lilv_node_free(input); lilv_node_free(output); lilv_node_free(cv);

    /* Range (for control ports) */
    if (pi->type == PM_PORT_CONTROL_IN || pi->type == PM_PORT_CONTROL_OUT) {
        LilvNode *def_n = NULL, *min_n = NULL, *max_n = NULL;
        lilv_port_get_range(plugin, port, &def_n, &min_n, &max_n);
        if (def_n) { pi->default_val = (float)lilv_node_as_float(def_n); lilv_node_free(def_n); }
        if (min_n) { pi->min         = (float)lilv_node_as_float(min_n); lilv_node_free(min_n); }
        if (max_n) { pi->max         = (float)lilv_node_as_float(max_n); lilv_node_free(max_n); }

        /* Properties: toggled, integer, enumeration */
        LilvNode *toggled_uri = lilv_new_uri(w, LV2_CORE__toggled);
        LilvNode *integer_uri = lilv_new_uri(w, LV2_CORE__integer);
        LilvNodes *props = lilv_port_get_properties(plugin, port);
        pi->toggled    = lilv_nodes_contains(props, toggled_uri);
        pi->integer    = lilv_nodes_contains(props, integer_uri);
        lilv_nodes_free(props);
        lilv_node_free(toggled_uri);
        lilv_node_free(integer_uri);

        /* Enumeration scale points */
        LilvScalePoints *sps = lilv_port_get_scale_points(plugin, port);
        if (sps) {
            pi->enumeration = true;
            LILV_FOREACH(scale_points, i, sps) {
                const LilvScalePoint *sp = lilv_scale_points_get(sps, i);
                if (pi->enum_count < 16) {
                    snprintf(pi->enum_labels[pi->enum_count], PM_NAME_MAX,
                             "%s", lilv_str(lilv_scale_point_get_label(sp)));
                    pi->enum_values[pi->enum_count] =
                        (float)lilv_node_as_float(lilv_scale_point_get_value(sp));
                    pi->enum_count++;
                }
            }
            lilv_scale_points_free(sps);
        }
    }
}

static void extract_plugin(const LilvPlugin *plugin, pm_plugin_info_t *pi)
{
    memset(pi, 0, sizeof(*pi));

    snprintf(pi->uri,  sizeof(pi->uri),
             "%s", lilv_str(lilv_plugin_get_uri(plugin)));

    LilvNode *name = lilv_plugin_get_name(plugin);
    snprintf(pi->name, sizeof(pi->name), "%s", lilv_str(name));
    lilv_node_free(name);

    /* Author */
    LilvNode *author = lilv_plugin_get_author_name(plugin);
    snprintf(pi->author, sizeof(pi->author), "%s", lilv_str(author));
    lilv_node_free(author);

    /* Category */
    const LilvPluginClass *cls = lilv_plugin_get_class(plugin);
    if (cls) {
        LilvNode *cls_label = lilv_plugin_class_get_label(cls);
        snprintf(pi->category, sizeof(pi->category), "%s", lilv_str(cls_label));
        /* Sub-category from parent */
        const LilvPluginClass *parent = lilv_plugin_class_get_parent_uri(cls) ? NULL : NULL;
        (void)parent;
    }

    /* Ports */
    uint32_t num_ports = lilv_plugin_get_num_ports(plugin);
    for (uint32_t i = 0; i < num_ports && pi->port_count < PM_PORT_MAX; i++) {
        LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
        pm_port_info_t *pinfo = &pi->ports[pi->port_count++];
        extract_port((LilvPlugin *)plugin, port, pinfo);

        switch (pinfo->type) {
            case PM_PORT_AUDIO_IN:    pi->audio_in_count++;  break;
            case PM_PORT_AUDIO_OUT:   pi->audio_out_count++; break;
            case PM_PORT_MIDI_IN:     pi->midi_in_count++;   break;
            case PM_PORT_MIDI_OUT:    pi->midi_out_count++;  break;
            case PM_PORT_CONTROL_IN:  pi->ctrl_in_count++;   break;
            default: break;
        }
    }
}

/* ─── JSON cache ─────────────────────────────────────────────────────────────── */

static void save_cache(const char *cache_path)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "plugins", arr);

    for (int i = 0; i < g_count; i++) {
        pm_plugin_info_t *p = &g_plugins[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "uri",      p->uri);
        cJSON_AddStringToObject(obj, "name",     p->name);
        cJSON_AddStringToObject(obj, "author",   p->author);
        cJSON_AddStringToObject(obj, "category", p->category);

        cJSON *ports = cJSON_CreateArray();
        for (int j = 0; j < p->port_count; j++) {
            pm_port_info_t *pt = &p->ports[j];
            cJSON *po = cJSON_CreateObject();
            cJSON_AddStringToObject(po, "symbol", pt->symbol);
            cJSON_AddStringToObject(po, "name",   pt->name);
            cJSON_AddNumberToObject(po, "type",   (double)pt->type);
            cJSON_AddNumberToObject(po, "min",    (double)pt->min);
            cJSON_AddNumberToObject(po, "max",    (double)pt->max);
            cJSON_AddNumberToObject(po, "default",(double)pt->default_val);
            cJSON_AddBoolToObject(po, "toggled",  pt->toggled);
            cJSON_AddBoolToObject(po, "integer",  pt->integer);
            cJSON_AddItemToArray(ports, po);
        }
        cJSON_AddItemToObject(obj, "ports", ports);
        cJSON_AddItemToArray(arr, obj);
    }

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (str) {
        FILE *f = fopen(cache_path, "w");
        if (f) { fputs(str, f); fclose(f); }
        free(str);
    }
}

static bool load_cache(const char *cache_path)
{
    FILE *f = fopen(cache_path, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;

    cJSON *arr = cJSON_GetObjectItem(root, "plugins");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return false; }

    int n = cJSON_GetArraySize(arr);
    g_plugins = calloc(n, sizeof(pm_plugin_info_t));
    if (!g_plugins) { cJSON_Delete(root); return false; }
    g_count = 0;

    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(obj)) continue;
        pm_plugin_info_t *p = &g_plugins[g_count++];

        cJSON *uri = cJSON_GetObjectItem(obj, "uri");
        cJSON *nm  = cJSON_GetObjectItem(obj, "name");
        cJSON *au  = cJSON_GetObjectItem(obj, "author");
        cJSON *cat = cJSON_GetObjectItem(obj, "category");
        if (cJSON_IsString(uri)) snprintf(p->uri,      sizeof(p->uri),      "%s", uri->valuestring);
        if (cJSON_IsString(nm))  snprintf(p->name,     sizeof(p->name),     "%s", nm->valuestring);
        if (cJSON_IsString(au))  snprintf(p->author,   sizeof(p->author),   "%s", au->valuestring);
        if (cJSON_IsString(cat)) snprintf(p->category, sizeof(p->category), "%s", cat->valuestring);

        cJSON *ports = cJSON_GetObjectItem(obj, "ports");
        if (cJSON_IsArray(ports)) {
            int np = cJSON_GetArraySize(ports);
            for (int j = 0; j < np && p->port_count < PM_PORT_MAX; j++) {
                cJSON *po = cJSON_GetArrayItem(ports, j);
                pm_port_info_t *pt = &p->ports[p->port_count++];
                cJSON *s = cJSON_GetObjectItem(po, "symbol");
                cJSON *t = cJSON_GetObjectItem(po, "type");
                cJSON *mn = cJSON_GetObjectItem(po, "min");
                cJSON *mx = cJSON_GetObjectItem(po, "max");
                cJSON *df = cJSON_GetObjectItem(po, "default");
                if (cJSON_IsString(s)) snprintf(pt->symbol, sizeof(pt->symbol), "%s", s->valuestring);
                if (cJSON_IsNumber(t)) pt->type = (pm_port_type_t)(int)t->valuedouble;
                if (cJSON_IsNumber(mn)) pt->min = (float)mn->valuedouble;
                if (cJSON_IsNumber(mx)) pt->max = (float)mx->valuedouble;
                if (cJSON_IsNumber(df)) pt->default_val = (float)df->valuedouble;
            }
        }
    }

    cJSON_Delete(root);
    printf("[plugin_manager] Loaded %d plugins from cache\n", g_count);
    return true;
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

int pm_init(const char *lv2_paths, const char *cache_path)
{
    /* Try cache first */
    if (cache_path && load_cache(cache_path)) return 0;

    g_world = lilv_world_new();
    if (!g_world) return -1;

    /* Load paths */
    if (lv2_paths) {
        char *paths = strdup(lv2_paths);
        char *tok = strtok(paths, ":");
        while (tok) {
            LilvNode *path = lilv_new_file_uri(g_world, NULL, tok);
            lilv_world_load_bundle(g_world, path);
            lilv_node_free(path);
            tok = strtok(NULL, ":");
        }
        free(paths);
    }
    lilv_world_load_all(g_world);

    const LilvPlugins *plugins = lilv_world_get_all_plugins(g_world);
    int total = (int)lilv_plugins_size(plugins);
    g_plugins = calloc(total, sizeof(pm_plugin_info_t));
    if (!g_plugins) return -1;
    g_count = 0;

    LILV_FOREACH(plugins, i, plugins) {
        const LilvPlugin *p = lilv_plugins_get(plugins, i);
        if (!lilv_plugin_verify(p)) continue;
        extract_plugin(p, &g_plugins[g_count++]);
    }

    printf("[plugin_manager] Discovered %d plugins\n", g_count);

    if (cache_path) save_cache(cache_path);
    return 0;
}

void pm_fini(void)
{
    free(g_plugins); g_plugins = NULL; g_count = 0;
    if (g_world) { lilv_world_free(g_world); g_world = NULL; }
}

int pm_plugin_count(void) { return g_count; }

const pm_plugin_info_t *pm_plugin_at(int index)
{
    if (index < 0 || index >= g_count) return NULL;
    return &g_plugins[index];
}

const pm_plugin_info_t *pm_plugin_by_uri(const char *uri)
{
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_plugins[i].uri, uri) == 0)
            return &g_plugins[i];
    return NULL;
}

int pm_categories(char (*cats)[PM_CAT_MAX], int max_cats)
{
    int count = 0;
    for (int i = 0; i < g_count; i++) {
        const char *cat = g_plugins[i].category;
        if (!cat[0]) continue;
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (strcmp(cats[j], cat) == 0) { found = true; break; }
        }
        if (!found && count < max_cats)
            snprintf(cats[count++], PM_CAT_MAX, "%s", cat);
    }
    return count;
}

int pm_plugins_in_category(const char *category, int *indices, int max)
{
    int count = 0;
    for (int i = 0; i < g_count && count < max; i++)
        if (strcmp(g_plugins[i].category, category) == 0)
            indices[count++] = i;
    return count;
}

int pm_search(const char *query, int *indices, int max)
{
    int count = 0;
    for (int i = 0; i < g_count && count < max; i++) {
        /* Case-insensitive substring search in name */
        const char *name = g_plugins[i].name;
        size_t qlen = strlen(query);
        size_t nlen = strlen(name);
        for (size_t j = 0; j + qlen <= nlen; j++) {
            bool match = true;
            for (size_t k = 0; k < qlen; k++) {
                if (tolower((unsigned char)name[j+k]) !=
                    tolower((unsigned char)query[k])) {
                    match = false; break;
                }
            }
            if (match) { indices[count++] = i; break; }
        }
    }
    return count;
}
