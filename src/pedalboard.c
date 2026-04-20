#include "pedalboard.h"
#include "lv2_utils.h"
#include "snapshot.h"
#include "plugin_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "cJSON.h"

#include <sord/sord.h>

/* ─── Helpers ───────────────────────────────────────────────────────────────── */

static void ensure_dir(const char *path)
{
    mkdir(path, 0755);
}

/* Strip "file://" prefix if present */
static const char *uri_to_path(const char *uri)
{
    if (strncmp(uri, "file://", 7) == 0) return uri + 7;
    return uri;
}

/* ─── Init ───────────────────────────────────────────────────────────────────── */

void pb_init(pedalboard_t *pb)
{
    memset(pb, 0, sizeof(*pb));
    pb->bpm = 120.0f;
    pb->bpb = 4.0f;
    pb->current_snapshot = -1;
}

/* ─── Plugin helpers ─────────────────────────────────────────────────────────── */

pb_plugin_t *pb_find_plugin(pedalboard_t *pb, int instance_id)
{
    for (int i = 0; i < pb->plugin_count; i++)
        if (pb->plugins[i].instance_id == instance_id)
            return &pb->plugins[i];
    return NULL;
}

pb_plugin_t *pb_find_plugin_by_symbol(pedalboard_t *pb, const char *symbol)
{
    for (int i = 0; i < pb->plugin_count; i++)
        if (strcmp(pb->plugins[i].symbol, symbol) == 0)
            return &pb->plugins[i];
    return NULL;
}

pb_plugin_t *pb_add_plugin(pedalboard_t *pb, int instance_id,
                            const char *symbol, const char *uri)
{
    if (pb->plugin_count >= PB_MAX_PLUGINS) return NULL;
    pb_plugin_t *p = &pb->plugins[pb->plugin_count++];
    memset(p, 0, sizeof(*p));
    p->instance_id = instance_id;
    snprintf(p->symbol,  sizeof(p->symbol),  "%s", symbol);
    snprintf(p->uri,     sizeof(p->uri),     "%s", uri);
    snprintf(p->label,   sizeof(p->label),   "%s", symbol);
    p->enabled = true;
    p->bypass_midi_channel = -1;
    p->bypass_midi_cc      = -1;
    /* Default canvas position for mod-ui compatibility — spaced horizontally */
    p->canvas_x = 100.0f + (pb->plugin_count - 1) * 220.0f;
    p->canvas_y = 100.0f;
    pb->modified = true;
    return p;
}

void pb_remove_plugin(pedalboard_t *pb, int instance_id)
{
    for (int i = 0; i < pb->plugin_count; i++) {
        if (pb->plugins[i].instance_id == instance_id) {
            memmove(&pb->plugins[i], &pb->plugins[i+1],
                    (pb->plugin_count - i - 1) * sizeof(pb_plugin_t));
            pb->plugin_count--;
            pb->modified = true;
            return;
        }
    }
}

int pb_add_connection(pedalboard_t *pb, const char *from, const char *to)
{
    if (pb->connection_count >= PB_MAX_CONNECTS) return -1;
    pb_connection_t *c = &pb->connections[pb->connection_count++];
    snprintf(c->from, sizeof(c->from), "%s", from);
    snprintf(c->to,   sizeof(c->to),   "%s", to);
    pb->modified = true;
    return 0;
}

void pb_remove_connection(pedalboard_t *pb, const char *from, const char *to)
{
    for (int i = 0; i < pb->connection_count; i++) {
        if (strcmp(pb->connections[i].from, from) == 0 &&
            strcmp(pb->connections[i].to,   to)   == 0) {
            memmove(&pb->connections[i], &pb->connections[i+1],
                    (pb->connection_count - i - 1) * sizeof(pb_connection_t));
            pb->connection_count--;
            pb->modified = true;
            return;
        }
    }
}

/* ─── TTL parsing (load) ─────────────────────────────────────────────────────── */

/* Extract symbol from a relative URI like "amp/Gain" → "amp" */
static void uri_symbol(const char *uri, char *sym, size_t symsz)
{
    const char *p = strrchr(uri, '/');
    const char *q = strchr(uri, '/');
    if (q && q == p) {
        /* One level: "amp" */
        snprintf(sym, symsz, "%.*s", (int)(q - uri), uri);
    } else if (!p) {
        snprintf(sym, symsz, "%s", uri);
    } else {
        /* Two levels: "amp/Gain" → "amp" */
        snprintf(sym, symsz, "%.*s", (int)(strchr(uri, '/') - uri), uri);
    }
}

static int pb_load_ttl(pedalboard_t *pb, const char *ttl_path)
{
    SordModel *m = lv2u_load_ttl(ttl_path);
    if (!m) return -1;

    SordNode *ingen_Block   = lv2u_uri(NS_INGEN "Block");
    SordNode *ingen_Arc     = lv2u_uri(NS_INGEN "Arc");
    SordNode *rdf_type      = lv2u_uri(NS_RDF   "type");
    SordNode *lv2_prototype = lv2u_uri(NS_LV2   "prototype");
    SordNode *lv2_port      = lv2u_uri(NS_LV2   "port");
    SordNode *ingen_value   = lv2u_uri(NS_INGEN "value");
    SordNode *ingen_enabled = lv2u_uri(NS_INGEN "enabled");
    SordNode *ingen_canvasX = lv2u_uri(NS_INGEN "canvasX");
    SordNode *ingen_canvasY = lv2u_uri(NS_INGEN "canvasY");
    SordNode *ingen_tail    = lv2u_uri(NS_INGEN "tail");
    SordNode *ingen_head    = lv2u_uri(NS_INGEN "head");
    SordNode *mod_label     = lv2u_uri(NS_MOD   "label");
    SordNode *lv2_symbol    = lv2u_uri(NS_LV2   "symbol");
    SordNode *pedal_preset  = lv2u_uri(NS_PEDAL "preset");
    SordNode *mod_snapshotable = lv2u_uri(NS_MOD "snapshotable");
    SordNode *midi_binding  = lv2u_uri(NS_MIDI  "binding");
    SordNode *midi_channel  = lv2u_uri(NS_MIDI  "channel");
    SordNode *midi_cc       = lv2u_uri(NS_MIDI  "controllerNumber");
    SordNode *lv2_minimum   = lv2u_uri(NS_LV2   "minimum");
    SordNode *lv2_maximum   = lv2u_uri(NS_LV2   "maximum");
    SordNode *doap_name     = lv2u_uri(NS_DOAP  "name");

    /* Pedalboard name — save pb_subj for later use in connection iteration */
    SordNode *pb_subj = NULL;
    {
        SordNode *pedal_pb = lv2u_uri(NS_PEDAL "Pedalboard");
        SordIter *it = lv2u_iter_type(m, pedal_pb);
        sord_node_free(lv2u_world(), pedal_pb);
        if (it && !sord_iter_end(it)) {
            SordQuad q; sord_iter_get(it, q);
            pb_subj = q[SORD_SUBJECT]; /* owned by model, valid until sord_free */
            lv2u_get_string(m, pb_subj, doap_name, pb->name, sizeof(pb->name));
        }
        if (it) sord_iter_free(it);
    }

    /* Plugin blocks */
    {
        SordIter *it = lv2u_iter_type(m, ingen_Block);
        while (it && !sord_iter_end(it)) {
            SordQuad q; sord_iter_get(it, q);
            SordNode *subj = q[SORD_SUBJECT];

            const uint8_t *subj_str = sord_node_get_string(subj);
            if (!subj_str) { sord_iter_next(it); continue; }

            /* symbol = last path component of subject URI */
            char sym[PB_SYMBOL_MAX];
            const char *last_slash = strrchr((const char *)subj_str, '/');
            snprintf(sym, sizeof(sym), "%s", last_slash ? last_slash + 1 : (const char *)subj_str);

            /* LV2 prototype (plugin URI) */
            char plugin_uri[PB_URI_MAX] = "";
            SordNode *proto = lv2u_get_object(m, subj, lv2_prototype);
            if (proto)
                snprintf(plugin_uri, sizeof(plugin_uri), "%s",
                         sord_node_get_string(proto));

            if (!plugin_uri[0]) { sord_iter_next(it); continue; }

            int instance_id = pb->plugin_count; /* will be reassigned on load */
            pb_plugin_t *plug = pb_add_plugin(pb, instance_id, sym, plugin_uri);
            if (!plug) { sord_iter_next(it); continue; }

            lv2u_get_float(m, subj, ingen_canvasX, &plug->canvas_x);
            lv2u_get_float(m, subj, ingen_canvasY, &plug->canvas_y);
            lv2u_get_bool(m,  subj, ingen_enabled, &plug->enabled);
            lv2u_get_string(m, subj, mod_label, plug->label, sizeof(plug->label));

            SordNode *preset_n = lv2u_get_object(m, subj, pedal_preset);
            if (preset_n)
                snprintf(plug->preset_uri, sizeof(plug->preset_uri), "%s",
                         sord_node_get_string(preset_n));

            /* Ports */
            SordQuad port_pat = { subj, lv2_port, NULL, NULL };
            SordIter *pit = sord_find(m, port_pat);
            while (pit && !sord_iter_end(pit)) {
                SordQuad pq; sord_iter_get(pit, pq);
                SordNode *port_node = pq[SORD_OBJECT];

                if (plug->port_count >= PB_MAX_PORTS) { sord_iter_next(pit); continue; }
                pb_port_t *port = &plug->ports[plug->port_count++];
                memset(port, 0, sizeof(*port));
                port->midi_channel = -1;
                port->midi_cc      = -1;

                /* symbol from port URI relative part */
                const uint8_t *puri = sord_node_get_string(port_node);
                if (puri) {
                    const char *sl = strrchr((const char *)puri, '/');
                    snprintf(port->symbol, sizeof(port->symbol),
                             "%s", sl ? sl + 1 : (const char *)puri);
                }

                lv2u_get_float(m, port_node, ingen_value, &port->value);
                lv2u_get_bool(m,  port_node, mod_snapshotable, &port->snapshotable);

                /* MIDI binding */
                SordNode *binding = lv2u_get_object(m, port_node, midi_binding);
                if (binding) {
                    float tmp;
                    SordNode *ch_n = lv2u_get_object(m, binding, midi_channel);
                    SordNode *cc_n = lv2u_get_object(m, binding, midi_cc);
                    if (ch_n) port->midi_channel = atoi((const char *)sord_node_get_string(ch_n));
                    if (cc_n) port->midi_cc      = atoi((const char *)sord_node_get_string(cc_n));
                    if (lv2u_get_float(m, binding, lv2_minimum, &tmp)) port->midi_min = tmp;
                    if (lv2u_get_float(m, binding, lv2_maximum, &tmp)) port->midi_max = tmp;
                }

                sord_iter_next(pit);
            }
            if (pit) sord_iter_free(pit);

            /* Patch parameters: look up plugin info, then read stored paths */
            {
                const pm_plugin_info_t *pm_info = pm_plugin_by_uri(plug->uri);
                if (pm_info) {
                    for (int pi = 0; pi < pm_info->patch_param_count
                                     && plug->patch_param_count < PB_MAX_PATCH_PARAMS; pi++) {
                        const pm_patch_param_t *pp = &pm_info->patch_params[pi];
                        SordNode *param_pred = lv2u_uri(pp->uri);
                        SordNode *val_node   = lv2u_get_object(m, subj, param_pred);
                        sord_node_free(lv2u_world(), param_pred);
                        if (!val_node) continue;

                        const char *path_str = (const char *)sord_node_get_string(val_node);
                        if (!path_str || !path_str[0]) continue;

                        pb_patch_t *pbp = &plug->patch_params[plug->patch_param_count++];
                        snprintf(pbp->uri,  sizeof(pbp->uri),  "%s", pp->uri);
                        snprintf(pbp->path, sizeof(pbp->path), "%s", path_str);
                    }
                }
            }

            sord_iter_next(it);
        }
        if (it) sord_iter_free(it);
    }

    /* Connections: iterate ingen:arc values from the pedalboard subject.
     * mod-ui writes connections as blank nodes listed under ingen:arc on the
     * pedalboard subject. We use SPO index {pb_subj, ingen:arc, ?} which is
     * always available, then look up tail/head from each arc blank node. */
    if (pb_subj) {
        SordNode *ingen_arc_pred = lv2u_uri(NS_INGEN "arc");
        SordQuad pat = { pb_subj, ingen_arc_pred, NULL, NULL };
        SordIter *it = sord_find(m, pat);
        while (it && !sord_iter_end(it)) {
            SordQuad q; sord_iter_get(it, q);
            SordNode *arc_node = q[SORD_OBJECT];

            SordNode *tail_node = lv2u_get_object(m, arc_node, ingen_tail);
            SordNode *head_node = lv2u_get_object(m, arc_node, ingen_head);

            if (tail_node && head_node) {
                const char *from = (const char *)sord_node_get_string(tail_node);
                const char *to   = (const char *)sord_node_get_string(head_node);
                if (from && to)
                    pb_add_connection(pb, from, to);
            }

            sord_iter_next(it);
        }
        if (it) sord_iter_free(it);
        sord_node_free(lv2u_world(), ingen_arc_pred);
    }

    /* Free cached URIs */
    sord_node_free(lv2u_world(), ingen_Block);
    sord_node_free(lv2u_world(), ingen_Arc);
    sord_node_free(lv2u_world(), rdf_type);
    sord_node_free(lv2u_world(), lv2_prototype);
    sord_node_free(lv2u_world(), lv2_port);
    sord_node_free(lv2u_world(), ingen_value);
    sord_node_free(lv2u_world(), ingen_enabled);
    sord_node_free(lv2u_world(), ingen_canvasX);
    sord_node_free(lv2u_world(), ingen_canvasY);
    sord_node_free(lv2u_world(), ingen_tail);
    sord_node_free(lv2u_world(), ingen_head);
    sord_node_free(lv2u_world(), mod_label);
    sord_node_free(lv2u_world(), lv2_symbol);
    sord_node_free(lv2u_world(), pedal_preset);
    sord_node_free(lv2u_world(), mod_snapshotable);
    sord_node_free(lv2u_world(), midi_binding);
    sord_node_free(lv2u_world(), midi_channel);
    sord_node_free(lv2u_world(), midi_cc);
    sord_node_free(lv2u_world(), lv2_minimum);
    sord_node_free(lv2u_world(), lv2_maximum);
    sord_node_free(lv2u_world(), doap_name);

    sord_free(m);
    return 0;
}

/* ─── Load pedalboard bundle ─────────────────────────────────────────────────── */

int pb_load(pedalboard_t *pb, const char *bundle_dir)
{
    pb_init(pb);
    snprintf(pb->path, sizeof(pb->path), "%s", bundle_dir);

    /* Read manifest to find main TTL file */
    char manifest_path[PB_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.ttl", bundle_dir);

    SordModel *manifest = lv2u_load_ttl(manifest_path);
    if (!manifest) {
        fprintf(stderr, "[pedalboard] Cannot load manifest: %s\n", manifest_path);
        return -1;
    }

    /* Find the .ttl entry in manifest */
    char main_ttl[PB_PATH_MAX] = "";
    {
        SordIter *it = sord_begin(manifest);
        while (it && !sord_iter_end(it)) {
            SordQuad q; sord_iter_get(it, q);
            const uint8_t *subj = sord_node_get_string(q[SORD_SUBJECT]);
            if (subj && strstr((const char *)subj, ".ttl")) {
                snprintf(main_ttl, sizeof(main_ttl), "%s", (const char *)subj);
                break;
            }
            sord_iter_next(it);
        }
        if (it) sord_iter_free(it);
    }
    sord_free(manifest);
    fprintf(stderr, "[pedalboard] main_ttl uri = '%s'\n", main_ttl);

    /* Extract pedalboard name from main TTL filename */
    if (main_ttl[0]) {
        const char *sl = strrchr(main_ttl, '/');
        const char *name = sl ? sl + 1 : main_ttl;
        snprintf(pb->name, sizeof(pb->name), "%s", name);
        /* Strip .ttl extension */
        char *dot = strrchr(pb->name, '.');
        if (dot) *dot = '\0';
    } else {
        /* Fallback: use directory name */
        const char *dir_name = strrchr(bundle_dir, '/');
        snprintf(pb->name, sizeof(pb->name), "%s", dir_name ? dir_name + 1 : bundle_dir);
        char *dot = strrchr(pb->name, '.');
        if (dot && strcmp(dot, ".pedalboard") == 0) *dot = '\0';
    }

    /* Load main TTL.
     * sord resolves relative URIs to absolute file:// URIs when parsing, so
     * main_ttl may be "file:///path/to/Foo.ttl" rather than just "Foo.ttl".
     * Convert file:// URI → filesystem path; fall back to bundle_dir/name.ttl. */
    char ttl_path[PB_PATH_MAX];
    if (main_ttl[0]) {
        if (strncmp(main_ttl, "file://", 7) == 0)
            snprintf(ttl_path, sizeof(ttl_path), "%s", main_ttl + 7);
        else
            snprintf(ttl_path, sizeof(ttl_path), "%s/%s", bundle_dir, main_ttl);
    } else {
        snprintf(ttl_path, sizeof(ttl_path), "%s/%s.ttl", bundle_dir, pb->name);
    }

    fprintf(stderr, "[pedalboard] loading ttl: %s\n", ttl_path);
    int r = pb_load_ttl(pb, ttl_path);
    fprintf(stderr, "[pedalboard] pb_load_ttl returned %d, plugins=%d conns=%d\n",
            r, pb->plugin_count, pb->connection_count);
    if (r < 0) return r;

    /* Load snapshots */
    char snap_path[PB_PATH_MAX];
    snprintf(snap_path, sizeof(snap_path), "%s/snapshots.json", bundle_dir);
    snapshot_load(snap_path, pb->snapshots, &pb->snapshot_count,
                  PB_MAX_SNAPSHOTS, &pb->current_snapshot);

    /* Load transport settings (optional — not a hard error if absent) */
    char trans_path[PB_PATH_MAX];
    snprintf(trans_path, sizeof(trans_path), "%s/transport.json", bundle_dir);
    FILE *tf = fopen(trans_path, "r");
    if (tf) {
        fseek(tf, 0, SEEK_END);
        long tsz = ftell(tf);
        rewind(tf);
        if (tsz > 0 && tsz < 4096) {
            char *tbuf = malloc((size_t)tsz + 1);
            if (tbuf) {
                fread(tbuf, 1, (size_t)tsz, tf);
                tbuf[tsz] = '\0';
                cJSON *tj = cJSON_Parse(tbuf);
                free(tbuf);
                if (tj) {
                    cJSON *jbpm = cJSON_GetObjectItem(tj, "bpm");
                    cJSON *jbpb = cJSON_GetObjectItem(tj, "bpb");
                    cJSON *jroll = cJSON_GetObjectItem(tj, "rolling");
                    cJSON *jsync = cJSON_GetObjectItem(tj, "sync");
                    if (cJSON_IsNumber(jbpm))  pb->bpm = (float)jbpm->valuedouble;
                    if (cJSON_IsNumber(jbpb))  pb->bpb = (float)jbpb->valuedouble;
                    if (cJSON_IsBool(jroll))   pb->transport_rolling = cJSON_IsTrue(jroll);
                    if (cJSON_IsString(jsync)) {
                        pb->transport_sync = strcmp(jsync->valuestring, "midi_clock_slave") == 0 ? 1 : 0;
                    }
                    cJSON_Delete(tj);
                }
            }
        }
        fclose(tf);
    }

    pb->modified = false;
    return 0;
}

/* ─── TTL generation (save) ──────────────────────────────────────────────────── */

/* Return the bundle-relative path of an absolute file:// URI.
 * E.g. bundle_base="file:///path/Test.pedalboard",
 *      abs="file:///path/Test.pedalboard/CabinetLoader/level" → "CabinetLoader/level"
 * Returns abs unchanged if it doesn't start with bundle_base+"/". */
static const char *bundle_rel(const char *abs, const char *bundle_base)
{
    size_t blen = strlen(bundle_base);
    if (strncmp(abs, bundle_base, blen) == 0 && abs[blen] == '/')
        return abs + blen + 1;
    return abs;
}

/* Write a bare Turtle literal for a float value (e.g. 2606.0) */
static void write_float(FILE *f, float v)
{
    /* Use %g to strip trailing zeros, then ensure at least one decimal point
     * so the value is unambiguously a decimal (not integer) in Turtle. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", (double)v);
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E'))
        strncat(buf, ".0", sizeof(buf) - strlen(buf) - 1);
    fputs(buf, f);
}

static int pb_save_ttl(pedalboard_t *pb, const char *ttl_path)
{
    FILE *f = fopen(ttl_path, "w");
    if (!f) return -1;

    /* bundle_base: used to compute bundle-relative URIs from absolute file:// ones */
    char bundle_base[PB_PATH_MAX];
    snprintf(bundle_base, sizeof(bundle_base), "file://%s", pb->path);

    /* ── Prefix declarations ── */
    fputs(
        "@prefix atom:  <http://lv2plug.in/ns/ext/atom#> .\n"
        "@prefix doap:  <http://usefulinc.com/ns/doap#> .\n"
        "@prefix ingen: <http://drobilla.net/ns/ingen#> .\n"
        "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix midi:  <http://lv2plug.in/ns/ext/midi#> .\n"
        "@prefix mod:   <http://moddevices.com/ns/mod#> .\n"
        "@prefix pedal: <http://moddevices.com/ns/modpedal#> .\n"
        "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n\n",
        f);

    /* ── Connections as blank-node arcs ── */
    for (int i = 0; i < pb->connection_count; i++) {
        pb_connection_t *conn = &pb->connections[i];
        const char *from = bundle_rel(conn->from, bundle_base);
        const char *to   = bundle_rel(conn->to,   bundle_base);
        fprintf(f, "_:b%d\n    ingen:tail <%s> ;\n    ingen:head <%s> .\n\n",
                i + 1, from, to);
    }

    /* ── Plugin blocks ── */
    for (int i = 0; i < pb->plugin_count; i++) {
        pb_plugin_t *plug = &pb->plugins[i];

        fprintf(f, "<%s>\n", plug->symbol);
        fprintf(f, "    ingen:canvasX ");  write_float(f, plug->canvas_x);
        fprintf(f, " ;\n    ingen:canvasY "); write_float(f, plug->canvas_y);
        fprintf(f, " ;\n    ingen:enabled %s ;\n", plug->enabled ? "true" : "false");
        fprintf(f, "    lv2:prototype <%s> ;\n", plug->uri);
        fprintf(f, "    mod:label \"%s\" ;\n", plug->label);
        fprintf(f, "    pedal:instanceNumber %d ;\n", i);
        if (plug->preset_uri[0])
            fprintf(f, "    pedal:preset <%s> ;\n", plug->preset_uri);
        else
            fprintf(f, "    pedal:preset <> ;\n");

        /* lv2:port list — comma-separated on a single predicate */
        if (plug->port_count > 0) {
            fputs("    lv2:port", f);
            for (int j = 0; j < plug->port_count; j++)
                fprintf(f, " %s<%s/%s>",
                        (j == 0) ? "" : " ,\n             ",
                        plug->symbol, plug->ports[j].symbol);
            fputs(" ;\n", f);
        }
        /* Patch parameters (file paths stored as block property) */
        for (int j = 0; j < plug->patch_param_count; j++) {
            pb_patch_t *pp = &plug->patch_params[j];
            if (!pp->path[0]) continue;
            fprintf(f, "    <%s> \"%s\" ;\n", pp->uri, pp->path);
        }

        fputs("    a ingen:Block .\n\n", f);

        /* Port value declarations */
        for (int j = 0; j < plug->port_count; j++) {
            pb_port_t *port = &plug->ports[j];
            fprintf(f, "<%s/%s>\n    ingen:value ", plug->symbol, port->symbol);
            write_float(f, port->value);
            fputs(" ;\n", f);
            if (port->midi_channel >= 0 && port->midi_cc >= 0) {
                fputs("    midi:binding [\n", f);
                fprintf(f, "        midi:channel %d ;\n", port->midi_channel);
                fprintf(f, "        midi:controllerNumber %d ;\n", port->midi_cc);
                if (port->midi_min != 0.0f || port->midi_max != 1.0f) {
                    fprintf(f, "        lv2:minimum ");
                    write_float(f, port->midi_min);
                    fputs(" ;\n", f);
                    fprintf(f, "        lv2:maximum ");
                    write_float(f, port->midi_max);
                    fputs(" ;\n", f);
                }
                fputs("        a midi:Controller ;\n    ] ;\n", f);
            }
            fprintf(f, "    mod:snapshotable %s ;\n    a lv2:ControlPort ,\n        lv2:InputPort .\n\n",
                    port->snapshotable ? "true" : "false");
        }

    }

    /* ── Graph subject <> ── */
    fprintf(f, "<>\n    doap:name \"%s\" ;\n    ingen:polyphony 1 ;\n", pb->name);

    /* ingen:arc list */
    if (pb->connection_count > 0) {
        fputs("    ingen:arc", f);
        for (int i = 0; i < pb->connection_count; i++)
            fprintf(f, " %s_:b%d", (i == 0) ? "" : ",\n              ", i + 1);
        fputs(" ;\n", f);
    }

    /* ingen:block list */
    if (pb->plugin_count > 0) {
        fputs("    ingen:block", f);
        for (int i = 0; i < pb->plugin_count; i++)
            fprintf(f, " %s<%s>", (i == 0) ? "" : ",\n               ", pb->plugins[i].symbol);
        fputs(" ;\n", f);
    }

    /* lv2:port list (system capture/playback ports) */
    const char *sys_ports[] = { "capture_1", "capture_2", "playback_1", "playback_2", NULL };
    fputs("    lv2:port", f);
    for (int i = 0; sys_ports[i]; i++)
        fprintf(f, " %s<%s>", (i == 0) ? "" : ",\n             ", sys_ports[i]);
    fputs(" ;\n", f);

    fputs("    a ingen:Graph ,\n        lv2:Plugin ,\n        pedal:Pedalboard .\n", f);

    fclose(f);
    return 0;
}

static int pb_save_manifest(pedalboard_t *pb)
{
    char manifest_path[PB_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.ttl", pb->path);
    FILE *f = fopen(manifest_path, "w");
    if (!f) return -1;

    /* Format matches mod-ui manifest exactly:
     *   - minimal prefixes (ingen, lv2, pedal, rdfs only)
     *   - rdfs:seeAlso <Name.ttl>  (mod-ui uses this to find the main TTL)
     *   - no doap: prefix (doap:name is in the main TTL, not the manifest) */
    fprintf(f,
        "@prefix ingen: <http://drobilla.net/ns/ingen#> .\n"
        "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix pedal: <http://moddevices.com/ns/modpedal#> .\n"
        "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "\n"
        "<%s.ttl>\n"
        "    lv2:prototype ingen:GraphPrototype ;\n"
        "    a lv2:Plugin ,\n"
        "        ingen:Graph ,\n"
        "        pedal:Pedalboard ;\n"
        "    rdfs:seeAlso <%s.ttl> .\n",
        pb->name, pb->name);

    fclose(f);
    return 0;
}

/* ─── Save ───────────────────────────────────────────────────────────────────── */

int pb_save(pedalboard_t *pb)
{
    ensure_dir(pb->path);

    if (pb_save_manifest(pb) < 0) return -1;

    char ttl_path[PB_PATH_MAX];
    snprintf(ttl_path, sizeof(ttl_path), "%s/%s.ttl", pb->path, pb->name);
    if (pb_save_ttl(pb, ttl_path) < 0) return -1;

    char snap_path[PB_PATH_MAX];
    snprintf(snap_path, sizeof(snap_path), "%s/snapshots.json", pb->path);
    if (snapshot_save(snap_path, pb->snapshots, pb->snapshot_count,
                      pb->current_snapshot) < 0) return -1;

    /* Save transport settings */
    char trans_path[PB_PATH_MAX];
    snprintf(trans_path, sizeof(trans_path), "%s/transport.json", pb->path);
    FILE *tf = fopen(trans_path, "w");
    if (tf) {
        cJSON *tj = cJSON_CreateObject();
        cJSON_AddNumberToObject(tj, "bpm",     (double)pb->bpm);
        cJSON_AddNumberToObject(tj, "bpb",     (double)pb->bpb);
        cJSON_AddBoolToObject  (tj, "rolling", pb->transport_rolling);
        cJSON_AddStringToObject(tj, "sync",
            pb->transport_sync == 1 ? "midi_clock_slave" : "none");
        char *out = cJSON_Print(tj);
        if (out) { fputs(out, tf); free(out); }
        cJSON_Delete(tj);
        fclose(tf);
    }

    pb->modified = false;
    return 0;
}

int pb_save_as(pedalboard_t *pb, const char *new_dir)
{
    /* Build old and new file:// prefixes so we can rewrite connection URIs */
    char old_pfx[PB_PATH_MAX + 8];
    snprintf(old_pfx, sizeof(old_pfx), "file://%s/", pb->path);
    size_t old_len = strlen(old_pfx);

    snprintf(pb->path, sizeof(pb->path), "%s", new_dir);

    char new_pfx[PB_PATH_MAX + 8];
    snprintf(new_pfx, sizeof(new_pfx), "file://%s/", pb->path);

    /* Rewrite any connection endpoint that starts with the old prefix */
    for (int i = 0; i < pb->connection_count; i++) {
        pb_connection_t *c = &pb->connections[i];
        if (strncmp(c->from, old_pfx, old_len) == 0) {
            char tmp[PB_URI_MAX];
            snprintf(tmp, sizeof(tmp), "%s%s", new_pfx, c->from + old_len);
            snprintf(c->from, sizeof(c->from), "%s", tmp);
        }
        if (strncmp(c->to, old_pfx, old_len) == 0) {
            char tmp[PB_URI_MAX];
            snprintf(tmp, sizeof(tmp), "%s%s", new_pfx, c->to + old_len);
            snprintf(c->to, sizeof(c->to), "%s", tmp);
        }
    }
    return pb_save(pb);
}

/* ─── Delete bundle ──────────────────────────────────────────────────────────── */

int pb_bundle_delete(const char *bundle_path)
{
    DIR *d = opendir(bundle_path);
    if (!d) return -1;

    struct dirent *ent;
    char fpath[PB_PATH_MAX];
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        snprintf(fpath, sizeof(fpath), "%s/%s", bundle_path, ent->d_name);
        /* Only remove regular files — ignore sub-directories if any */
        if (ent->d_type == DT_DIR) continue;
        unlink(fpath);
    }
    closedir(d);
    return rmdir(bundle_path) == 0 ? 0 : -1;
}

/* ─── List bundles ───────────────────────────────────────────────────────────── */

int pb_list(const char *base_dir, char **paths, int max_paths)
{
    DIR *d = opendir(base_dir);
    if (!d) return 0;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) && count < max_paths) {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len > 11 && strcmp(ent->d_name + len - 11, ".pedalboard") == 0) {
            char *full = malloc(PB_PATH_MAX);
            if (full) {
                snprintf(full, PB_PATH_MAX, "%s/%s", base_dir, ent->d_name);
                paths[count++] = full;
            }
        }
    }
    closedir(d);
    return count;
}

int pb_read_name(const char *bundle_path, char *out, size_t out_size)
{
    /* Derive the main TTL filename from the bundle directory name */
    const char *dir_name = strrchr(bundle_path, '/');
    dir_name = dir_name ? dir_name + 1 : bundle_path;

    char stem[PB_NAME_MAX];
    snprintf(stem, sizeof(stem), "%s", dir_name);
    char *dot = strrchr(stem, '.');
    if (dot && strcmp(dot, ".pedalboard") == 0) *dot = '\0';

    char ttl_path[PB_PATH_MAX + PB_NAME_MAX + 4];
    snprintf(ttl_path, sizeof(ttl_path), "%s/%s.ttl", bundle_path, stem);

    FILE *f = fopen(ttl_path, "r");
    if (!f) {
        snprintf(out, out_size, "%s", stem);
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "doap:name");
        if (!p) continue;
        p = strchr(p, '"');
        if (!p) continue;
        p++;
        char *end = strchr(p, '"');
        if (!end) continue;
        size_t len = (size_t)(end - p);
        if (len >= out_size) len = out_size - 1;
        memcpy(out, p, len);
        out[len] = '\0';
        fclose(f);
        return 0;
    }
    fclose(f);
    snprintf(out, out_size, "%s", stem);
    return -1;
}

/* ─── Snapshot wrappers ──────────────────────────────────────────────────────── */

/* Capture current live state into snap (helper shared by save/overwrite) */
static void snapshot_capture(pb_snapshot_t *snap, const pedalboard_t *pb)
{
    snap->plugin_count = 0;
    for (int i = 0; i < pb->plugin_count; i++) {
        const pb_plugin_t *plug = &pb->plugins[i];
        snap_plugin_t *sp = &snap->plugins[snap->plugin_count++];
        memset(sp, 0, sizeof(*sp));
        snprintf(sp->symbol, sizeof(sp->symbol), "%s", plug->symbol);
        sp->bypassed = !plug->enabled;
        snprintf(sp->preset_uri, sizeof(sp->preset_uri), "%s", plug->preset_uri);

        /* Regular control ports */
        for (int j = 0; j < plug->port_count; j++) {
            if (!plug->ports[j].snapshotable) continue;
            if (sp->param_count >= PB_MAX_PORTS) break;
            snap_param_t *param = &sp->params[sp->param_count++];
            snprintf(param->symbol, sizeof(param->symbol), "%s", plug->ports[j].symbol);
            param->value = plug->ports[j].value;
        }

        /* Patch params (file paths) */
        for (int j = 0; j < plug->patch_param_count
                         && sp->patch_param_count < SNAP_MAX_PATCH_PARAMS; j++) {
            if (!plug->patch_params[j].path[0]) continue;
            snap_patch_t *pp = &sp->patch_params[sp->patch_param_count++];
            snprintf(pp->uri,  sizeof(pp->uri),  "%s", plug->patch_params[j].uri);
            snprintf(pp->path, sizeof(pp->path), "%s", plug->patch_params[j].path);
        }
    }
}

int pb_snapshot_save_current(pedalboard_t *pb, const char *name)
{
    if (pb->snapshot_count >= PB_MAX_SNAPSHOTS) return -1;
    pb_snapshot_t *snap = &pb->snapshots[pb->snapshot_count];
    memset(snap, 0, sizeof(*snap));
    snprintf(snap->name, sizeof(snap->name), "%s", name);
    snapshot_capture(snap, pb);
    pb->current_snapshot = pb->snapshot_count;
    pb->snapshot_count++;
    pb->modified = true;
    return pb->current_snapshot;
}

int pb_snapshot_overwrite(pedalboard_t *pb, int index)
{
    if (index < 0 || index >= pb->snapshot_count) return -1;
    pb_snapshot_t *snap = &pb->snapshots[index];
    char saved_name[PB_NAME_MAX];
    snprintf(saved_name, sizeof(saved_name), "%s", snap->name);
    memset(snap, 0, sizeof(*snap));
    snprintf(snap->name, sizeof(snap->name), "%s", saved_name);
    snapshot_capture(snap, pb);
    pb->current_snapshot = index;
    pb->modified = true;
    return 0;
}

int pb_snapshot_load(pedalboard_t *pb, int index)
{
    if (index < 0 || index >= pb->snapshot_count) return -1;
    pb->current_snapshot = index;
    pb_snapshot_t *snap = &pb->snapshots[index];

    for (int i = 0; i < snap->plugin_count; i++) {
        snap_plugin_t *sp = &snap->plugins[i];
        pb_plugin_t *plug = pb_find_plugin_by_symbol(pb, sp->symbol);
        if (!plug) continue;
        plug->enabled = !sp->bypassed;
        if (sp->preset_uri[0])
            snprintf(plug->preset_uri, sizeof(plug->preset_uri), "%s", sp->preset_uri);

        /* Regular control ports */
        for (int j = 0; j < sp->param_count; j++) {
            for (int k = 0; k < plug->port_count; k++) {
                if (strcmp(plug->ports[k].symbol, sp->params[j].symbol) == 0) {
                    plug->ports[k].value = sp->params[j].value;
                    break;
                }
            }
        }

        /* Patch params (file paths) */
        for (int j = 0; j < sp->patch_param_count; j++) {
            for (int k = 0; k < plug->patch_param_count; k++) {
                if (strcmp(plug->patch_params[k].uri, sp->patch_params[j].uri) == 0) {
                    snprintf(plug->patch_params[k].path,
                             sizeof(plug->patch_params[k].path),
                             "%s", sp->patch_params[j].path);
                    break;
                }
            }
            /* If not found, add it */
            if (plug->patch_param_count < PB_MAX_PATCH_PARAMS) {
                bool found = false;
                for (int k = 0; k < plug->patch_param_count; k++)
                    if (strcmp(plug->patch_params[k].uri, sp->patch_params[j].uri) == 0)
                        { found = true; break; }
                if (!found) {
                    pb_patch_t *pp = &plug->patch_params[plug->patch_param_count++];
                    snprintf(pp->uri,  sizeof(pp->uri),  "%s", sp->patch_params[j].uri);
                    snprintf(pp->path, sizeof(pp->path), "%s", sp->patch_params[j].path);
                }
            }
        }
    }
    return 0;
}

void pb_snapshot_delete(pedalboard_t *pb, int index)
{
    if (index < 0 || index >= pb->snapshot_count) return;
    memmove(&pb->snapshots[index], &pb->snapshots[index+1],
            (pb->snapshot_count - index - 1) * sizeof(pb_snapshot_t));
    pb->snapshot_count--;
    if (pb->current_snapshot >= pb->snapshot_count)
        pb->current_snapshot = pb->snapshot_count - 1;
    pb->modified = true;
}

void pb_snapshot_rename(pedalboard_t *pb, int index, const char *name)
{
    if (index < 0 || index >= pb->snapshot_count) return;
    snprintf(pb->snapshots[index].name, sizeof(pb->snapshots[index].name), "%s", name);
    pb->modified = true;
}
