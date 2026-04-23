#include "plugin_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include "cJSON.h"
#include "lv2_utils.h"

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
    lv2u_normalize_quotes(pi->name);
    lilv_node_free(name_n);

    /* Type */
    LilvNode *audio       = lilv_new_uri(w, LV2_CORE__AudioPort);
    LilvNode *atom_port   = lilv_new_uri(w, LV2_ATOM__AtomPort);
    LilvNode *atom_sup    = lilv_new_uri(w, LV2_ATOM__supports);
    LilvNode *midi_event  = lilv_new_uri(w, LV2_MIDI__MidiEvent);
    LilvNode *ctrl        = lilv_new_uri(w, LV2_CORE__ControlPort);
    LilvNode *input       = lilv_new_uri(w, LV2_CORE__InputPort);
    LilvNode *output      = lilv_new_uri(w, LV2_CORE__OutputPort);
    LilvNode *cv          = lilv_new_uri(w, LV2_CORE__CVPort);

    bool is_in  = lilv_port_is_a(plugin, port, input);
    bool is_out = lilv_port_is_a(plugin, port, output);

    if (lilv_port_is_a(plugin, port, audio)) {
        pi->type = is_in ? PM_PORT_AUDIO_IN : PM_PORT_AUDIO_OUT;
    } else if (lilv_port_is_a(plugin, port, atom_port)) {
        /* Only treat as MIDI if the port explicitly supports midi:MidiEvent.
         * Ports that only carry atom:Sequence/patch notifications ("Notify")
         * are internal and should not appear in the user connection panel. */
        LilvNodes *sup = lilv_port_get_value(plugin, port, atom_sup);
        bool is_midi = sup && lilv_nodes_contains(sup, midi_event);
        lilv_nodes_free(sup);
        pi->type = is_midi ? (is_in ? PM_PORT_MIDI_IN : PM_PORT_MIDI_OUT)
                           : (is_in ? PM_PORT_CV_IN   : PM_PORT_CV_OUT);
    } else if (lilv_port_is_a(plugin, port, cv)) {
        pi->type = is_in ? PM_PORT_CV_IN : PM_PORT_CV_OUT;
    } else {
        pi->type = is_in ? PM_PORT_CONTROL_IN : PM_PORT_CONTROL_OUT;
    }

    lilv_node_free(audio); lilv_node_free(atom_port); lilv_node_free(atom_sup);
    lilv_node_free(midi_event); lilv_node_free(ctrl);
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

/* ─── Direct TTL name reader (manifest.ttl → rdfs:seeAlso → doap:name) ────────
 * More reliable than lilv_plugin_get_name() which can return the URI or an
 * empty string for plugins whose doap:name lives only in the bundle TTL. */

static bool bundle_uri_to_path(const char *uri, char *out, size_t outsz)
{
    const char *p = uri;
    if (strncmp(p, "file://", 7) == 0) p += 7;
    size_t len = strlen(p);
    if (len > 0 && p[len-1] == '/') len--;
    if (len == 0 || len >= outsz) return false;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/* Scan manifest.ttl for local .ttl <refs>, skip modgui.ttl, deduplicate. */
static int collect_seealso_ttls(const char *bundle_path, char names[][256], int max)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/manifest.ttl", bundle_path);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char buf[32768];
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    buf[n] = '\0';

    int count = 0;
    const char *p = buf;
    while (count < max) {
        p = strchr(p, '<');
        if (!p) break;
        p++;
        const char *end = strchr(p, '>');
        if (!end) break;
        size_t len = (size_t)(end - p);
        /* Local .ttl filename: no '/' (path), no ':' (URI scheme) */
        if (len >= 5 && len < 256 &&
            !memchr(p, '/', len) && !memchr(p, ':', len) &&
            memcmp(end - 4, ".ttl", 4) == 0) {
            char name[256];
            memcpy(name, p, len);
            name[len] = '\0';
            if (strcmp(name, "modgui.ttl") != 0) {
                bool dup = false;
                for (int i = 0; i < count; i++)
                    if (strcmp(names[i], name) == 0) { dup = true; break; }
                if (!dup)
                    snprintf(names[count++], 256, "%s", name);
            }
        }
        p = end + 1;
    }
    return count;
}

/* Return the first doap:name "..." found in a TTL file. */
static bool ttl_read_doap_name(const char *ttl_path, char *out, size_t outsz)
{
    FILE *f = fopen(ttl_path, "r");
    if (!f) return false;
    char line[1024];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "doap:name");
        if (!p) continue;
        p = strchr(p, '"');
        if (!p) continue;
        p++;
        char *end = strchr(p, '"');
        if (!end) continue;
        size_t len = (size_t)(end - p);
        if (len >= outsz) len = outsz - 1;
        memcpy(out, p, len);
        out[len] = '\0';
        found = true;
        break;
    }
    fclose(f);
    return found;
}

/* Read plugin display name via manifest.ttl → rdfs:seeAlso → doap:name. */
static bool plugin_name_from_bundle(const LilvPlugin *plugin, char *out, size_t outsz)
{
    const LilvNode *buri = lilv_plugin_get_bundle_uri(plugin);
    if (!buri) return false;

    char bundle_path[1024];
    if (!bundle_uri_to_path(lilv_str(buri), bundle_path, sizeof(bundle_path)))
        return false;

    char ttl_names[8][256];
    int n = collect_seealso_ttls(bundle_path, ttl_names, 8);
    for (int i = 0; i < n; i++) {
        char ttl_path[1280];
        snprintf(ttl_path, sizeof(ttl_path), "%s/%s", bundle_path, ttl_names[i]);
        if (ttl_read_doap_name(ttl_path, out, outsz)) {
            lv2u_normalize_quotes(out);
            return true;
        }
    }
    return false;
}

static void extract_plugin(const LilvPlugin *plugin, pm_plugin_info_t *pi)
{
    memset(pi, 0, sizeof(*pi));

    snprintf(pi->uri,  sizeof(pi->uri),
             "%s", lilv_str(lilv_plugin_get_uri(plugin)));

    /* Prefer doap:name from the bundle TTL; fall back to lilv if not found. */
    if (!plugin_name_from_bundle(plugin, pi->name, sizeof(pi->name))) {
        LilvNode *name = lilv_plugin_get_name(plugin);
        snprintf(pi->name, sizeof(pi->name), "%s", lilv_str(name));
        lv2u_normalize_quotes(pi->name);
        lilv_node_free(name);
    }

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

    /* patch:writable parameters (e.g. file path params) */
    {
        LilvNode *pw_uri    = lilv_new_uri(g_world, LV2_PATCH__writable);
        LilvNode *rdfs_rng  = lilv_new_uri(g_world, "http://www.w3.org/2000/01/rdf-schema#range");
        LilvNode *rdfs_lbl  = lilv_new_uri(g_world, "http://www.w3.org/2000/01/rdf-schema#label");
        LilvNode *atom_path = lilv_new_uri(g_world, LV2_ATOM__Path);
        LilvNode *mod_ft    = lilv_new_uri(g_world, "http://moddevices.com/ns/mod#fileTypes");

        LilvNodes *writables = lilv_plugin_get_value(plugin, pw_uri);
        if (writables) {
            LILV_FOREACH(nodes, it, writables) {
                if (pi->patch_param_count >= PM_PATCH_MAX) break;
                const LilvNode *param_uri = lilv_nodes_get(writables, it);

                /* Check rdfs:range == atom:Path */
                LilvNode *range = lilv_world_get(g_world, param_uri, rdfs_rng, NULL);
                if (!range || !lilv_node_equals(range, atom_path)) {
                    lilv_node_free(range);
                    continue;
                }
                lilv_node_free(range);

                pm_patch_param_t *pp = &pi->patch_params[pi->patch_param_count++];
                memset(pp, 0, sizeof(*pp));
                snprintf(pp->uri, sizeof(pp->uri), "%s", lilv_str(param_uri));

                LilvNode *lbl = lilv_world_get(g_world, param_uri, rdfs_lbl, NULL);
                if (lbl) {
                    snprintf(pp->label, sizeof(pp->label), "%s", lilv_str(lbl));
                    lilv_node_free(lbl);
                }
                LilvNode *ft = lilv_world_get(g_world, param_uri, mod_ft, NULL);
                if (ft) {
                    snprintf(pp->file_types, sizeof(pp->file_types), "%s", lilv_str(ft));
                    lilv_node_free(ft);
                }

                /* Default browse dir: plugin bundle + "models"
                 * Bundle URI is "file:///path/to/plugin.lv2/" */
                const LilvNode *bundle = lilv_plugin_get_bundle_uri(plugin);
                if (bundle) {
                    const char *buri = lilv_str(bundle);
                    /* Strip "file://" prefix */
                    const char *bpath = buri;
                    if (strncmp(bpath, "file://", 7) == 0) bpath += 7;
                    /* Strip trailing slash */
                    size_t blen = strlen(bpath);
                    if (blen > 0 && bpath[blen-1] == '/')
                        snprintf(pp->default_dir, sizeof(pp->default_dir),
                                 "%.*smodels", (int)(blen-1), bpath);
                    else
                        snprintf(pp->default_dir, sizeof(pp->default_dir),
                                 "%s/models", bpath);
                }
            }
            lilv_nodes_free(writables);
        }
        lilv_node_free(pw_uri);
        lilv_node_free(rdfs_rng);
        lilv_node_free(rdfs_lbl);
        lilv_node_free(atom_path);
        lilv_node_free(mod_ft);
    }

    /* modgui:thumbnail — path to plugin preview PNG */
    {
        LilvNode *modgui_gui  = lilv_new_uri(g_world, "http://moddevices.com/ns/modgui#gui");
        LilvNode *modgui_thumb = lilv_new_uri(g_world, "http://moddevices.com/ns/modgui#thumbnail");
        LilvNodes *guis = lilv_plugin_get_value(plugin, modgui_gui);
        if (guis) {
            LILV_FOREACH(nodes, it, guis) {
                const LilvNode *gui = lilv_nodes_get(guis, it);
                LilvNode *thumb = lilv_world_get(g_world, gui, modgui_thumb, NULL);
                if (thumb) {
                    const char *uri = lilv_str(thumb);
                    if (strncmp(uri, "file://", 7) == 0)
                        snprintf(pi->thumbnail_path, sizeof(pi->thumbnail_path), "%s", uri + 7);
                    lilv_node_free(thumb);
                    break;
                }
            }
            lilv_nodes_free(guis);
        }
        lilv_node_free(modgui_thumb);
        lilv_node_free(modgui_gui);
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
            case PM_PORT_CV_IN:       pi->cv_in_count++;     break;
            case PM_PORT_CV_OUT:      pi->cv_out_count++;    break;
            default: break;
        }
    }
}

/* ─── JSON cache ─────────────────────────────────────────────────────────────── */

/* Bump this when the cache schema changes to force automatic regeneration */
#define CACHE_VERSION 7

static void save_cache(const char *cache_path)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", CACHE_VERSION);
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
            cJSON_AddStringToObject(po, "symbol",  pt->symbol);
            cJSON_AddStringToObject(po, "name",    pt->name);
            cJSON_AddNumberToObject(po, "type",    (double)pt->type);
            cJSON_AddNumberToObject(po, "min",     (double)pt->min);
            cJSON_AddNumberToObject(po, "max",     (double)pt->max);
            cJSON_AddNumberToObject(po, "default", (double)pt->default_val);
            cJSON_AddBoolToObject(po, "toggled",   pt->toggled);
            cJSON_AddBoolToObject(po, "integer",   pt->integer);
            cJSON_AddBoolToObject(po, "enumeration", pt->enumeration);
            if (pt->enum_count > 0) {
                cJSON *elabels = cJSON_CreateArray();
                cJSON *evalues = cJSON_CreateArray();
                for (int k = 0; k < pt->enum_count; k++) {
                    cJSON_AddItemToArray(elabels,
                        cJSON_CreateString(pt->enum_labels[k]));
                    cJSON_AddItemToArray(evalues,
                        cJSON_CreateNumber((double)pt->enum_values[k]));
                }
                cJSON_AddItemToObject(po, "enum_labels", elabels);
                cJSON_AddItemToObject(po, "enum_values", evalues);
            }
            cJSON_AddItemToArray(ports, po);
        }
        cJSON_AddItemToObject(obj, "ports", ports);

        /* patch:writable parameters */
        if (p->patch_param_count > 0) {
            cJSON *pps = cJSON_CreateArray();
            for (int j = 0; j < p->patch_param_count; j++) {
                pm_patch_param_t *pp = &p->patch_params[j];
                cJSON *ppo = cJSON_CreateObject();
                cJSON_AddStringToObject(ppo, "uri",         pp->uri);
                cJSON_AddStringToObject(ppo, "label",       pp->label);
                cJSON_AddStringToObject(ppo, "file_types",  pp->file_types);
                cJSON_AddStringToObject(ppo, "default_dir", pp->default_dir);
                cJSON_AddItemToArray(pps, ppo);
            }
            cJSON_AddItemToObject(obj, "patch_params", pps);
        }

        cJSON_AddStringToObject(obj, "thumbnail_path", p->thumbnail_path);
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

    /* Reject cache if version doesn't match — forces lilv rescan */
    cJSON *ver = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsNumber(ver) || (int)ver->valuedouble != CACHE_VERSION) {
        cJSON_Delete(root);
        printf("[plugin_manager] Cache version mismatch — rescanning LV2 plugins\n");
        return false;
    }

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
        cJSON *tp = cJSON_GetObjectItem(obj, "thumbnail_path");
        if (cJSON_IsString(tp)) snprintf(p->thumbnail_path, sizeof(p->thumbnail_path), "%s", tp->valuestring);

        cJSON *ports = cJSON_GetObjectItem(obj, "ports");
        if (cJSON_IsArray(ports)) {
            int np = cJSON_GetArraySize(ports);
            for (int j = 0; j < np && p->port_count < PM_PORT_MAX; j++) {
                cJSON *po = cJSON_GetArrayItem(ports, j);
                pm_port_info_t *pt = &p->ports[p->port_count++];
                cJSON *s   = cJSON_GetObjectItem(po, "symbol");
                cJSON *nm  = cJSON_GetObjectItem(po, "name");
                cJSON *t   = cJSON_GetObjectItem(po, "type");
                cJSON *mn  = cJSON_GetObjectItem(po, "min");
                cJSON *mx  = cJSON_GetObjectItem(po, "max");
                cJSON *df  = cJSON_GetObjectItem(po, "default");
                cJSON *tog = cJSON_GetObjectItem(po, "toggled");
                cJSON *itg = cJSON_GetObjectItem(po, "integer");
                cJSON *enu = cJSON_GetObjectItem(po, "enumeration");
                if (cJSON_IsString(s))  snprintf(pt->symbol, sizeof(pt->symbol), "%s", s->valuestring);
                if (cJSON_IsString(nm)) snprintf(pt->name,   sizeof(pt->name),   "%s", nm->valuestring);
                if (cJSON_IsNumber(t))  pt->type        = (pm_port_type_t)(int)t->valuedouble;
                if (cJSON_IsNumber(mn)) pt->min         = (float)mn->valuedouble;
                if (cJSON_IsNumber(mx)) pt->max         = (float)mx->valuedouble;
                if (cJSON_IsNumber(df)) pt->default_val = (float)df->valuedouble;
                if (cJSON_IsBool(tog))  pt->toggled     = cJSON_IsTrue(tog);
                if (cJSON_IsBool(itg))  pt->integer     = cJSON_IsTrue(itg);
                if (cJSON_IsBool(enu))  pt->enumeration = cJSON_IsTrue(enu);
                cJSON *elabels = cJSON_GetObjectItem(po, "enum_labels");
                cJSON *evalues = cJSON_GetObjectItem(po, "enum_values");
                if (cJSON_IsArray(elabels) && cJSON_IsArray(evalues)) {
                    int nc = cJSON_GetArraySize(elabels);
                    if (nc > 16) nc = 16;
                    for (int k = 0; k < nc; k++) {
                        cJSON *lbl = cJSON_GetArrayItem(elabels, k);
                        cJSON *val = cJSON_GetArrayItem(evalues, k);
                        if (cJSON_IsString(lbl))
                            snprintf(pt->enum_labels[k], PM_NAME_MAX, "%s", lbl->valuestring);
                        if (cJSON_IsNumber(val))
                            pt->enum_values[k] = (float)val->valuedouble;
                    }
                    pt->enum_count = nc;
                }
            }
        }

        /* Derive CV counts from port types (not stored in cache, computed here) */
        for (int j = 0; j < p->port_count; j++) {
            if (p->ports[j].type == PM_PORT_CV_IN)  p->cv_in_count++;
            if (p->ports[j].type == PM_PORT_CV_OUT) p->cv_out_count++;
        }

        /* patch:writable parameters */
        cJSON *pps = cJSON_GetObjectItem(obj, "patch_params");
        if (cJSON_IsArray(pps)) {
            int npp = cJSON_GetArraySize(pps);
            for (int j = 0; j < npp && p->patch_param_count < PM_PATCH_MAX; j++) {
                cJSON *ppo = cJSON_GetArrayItem(pps, j);
                pm_patch_param_t *pp = &p->patch_params[p->patch_param_count++];
                memset(pp, 0, sizeof(*pp));
                cJSON *u  = cJSON_GetObjectItem(ppo, "uri");
                cJSON *lb = cJSON_GetObjectItem(ppo, "label");
                cJSON *ft = cJSON_GetObjectItem(ppo, "file_types");
                cJSON *dd = cJSON_GetObjectItem(ppo, "default_dir");
                if (cJSON_IsString(u))  snprintf(pp->uri,         sizeof(pp->uri),         "%s", u->valuestring);
                if (cJSON_IsString(lb)) snprintf(pp->label,       sizeof(pp->label),       "%s", lb->valuestring);
                if (cJSON_IsString(ft)) snprintf(pp->file_types,  sizeof(pp->file_types),  "%s", ft->valuestring);
                if (cJSON_IsString(dd)) snprintf(pp->default_dir, sizeof(pp->default_dir), "%s", dd->valuestring);
            }
        }
    }

    cJSON_Delete(root);
    printf("[plugin_manager] Loaded %d plugins from cache\n", g_count);
    return true;
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

/* Return true only if cache_path exists AND is newer than every directory in
 * the colon-separated lv2_paths list and /usr/lib/lv2 (and LV2_PATH env).
 * A newly installed plugin updates its parent directory's mtime, which makes
 * the cache stale and forces a lilv rescan on the next startup. */
static bool cache_is_fresh(const char *cache_path, const char *lv2_paths)
{
    struct stat cs;
    if (stat(cache_path, &cs) != 0) return false;
    time_t cache_mtime = cs.st_mtime;

    const char *sources[3] = { "/usr/lib/lv2", lv2_paths, getenv("LV2_PATH") };
    for (int s = 0; s < 3; s++) {
        if (!sources[s]) continue;
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s", sources[s]);
        char *save = NULL;
        char *tok  = strtok_r(buf, ":", &save);
        while (tok) {
            if (tok[0]) {
                struct stat ds;
                if (stat(tok, &ds) == 0 && ds.st_mtime > cache_mtime) {
                    printf("[plugin_manager] '%s' newer than cache — rescanning\n", tok);
                    return false;
                }
            }
            tok = strtok_r(NULL, ":", &save);
        }
    }
    return true;
}

int pm_init(const char *lv2_paths, const char *cache_path)
{
    /* Cleanup previous state if reinitializing */
    if (g_plugins) { free(g_plugins); g_plugins = NULL; g_count = 0; }
    if (g_world)   { lilv_world_free(g_world); g_world = NULL; }

    /* Try cache only if it is newer than all LV2 directories being scanned.
     * A new plugin installation updates the parent directory mtime and
     * automatically triggers a full lilv rescan. */
    if (cache_path && cache_is_fresh(cache_path, lv2_paths) && load_cache(cache_path))
        return 0;

    g_world = lilv_world_new();
    if (!g_world) return -1;

    /* Ensure system LV2 bundles are on the path.
     * The service may set LV2_PATH to only the user plugin directory, which
     * omits /usr/lib/lv2 where lv2core.lv2 lives.  Without lv2core the
     * plugin class hierarchy (lv2:ReverbPlugin, etc.) is not loaded and
     * lilv_plugin_get_class() falls back to the root "Plugin" for everything.
     * We prepend the system path so the class definitions load before plugins. */
    {
        const char *env = getenv("LV2_PATH");
        const char *sys = "/usr/lib/lv2";
        if (!env || !strstr(env, sys)) {
            char extended[4096];
            if (env && env[0])
                snprintf(extended, sizeof(extended), "%s:%s", sys, env);
            else
                snprintf(extended, sizeof(extended), "%s", sys);
            setenv("LV2_PATH", extended, 1);
        }
        /* Also include any custom paths passed in as parameter */
        if (lv2_paths) {
            env = getenv("LV2_PATH");
            char extended[4096];
            snprintf(extended, sizeof(extended), "%s:%s", env ? env : "", lv2_paths);
            setenv("LV2_PATH", extended, 1);
        }
    }
    lilv_world_load_all(g_world);

    const LilvPlugins *plugins = lilv_world_get_all_plugins(g_world);
    int total = (int)lilv_plugins_size(plugins);
    g_plugins = calloc(total, sizeof(pm_plugin_info_t));
    if (!g_plugins) { lilv_world_free(g_world); g_world = NULL; return -1; }
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
