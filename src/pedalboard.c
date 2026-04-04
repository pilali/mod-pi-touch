#include "pedalboard.h"
#include "lv2_utils.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

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

    pb->modified = false;
    return 0;
}

/* ─── TTL generation (save) ──────────────────────────────────────────────────── */

static int pb_save_ttl(pedalboard_t *pb, const char *ttl_path)
{
    SordModel *m = sord_new(lv2u_world(), SORD_SPO, false);
    SordNode *rdf_type    = lv2u_uri(NS_RDF   "type");
    SordNode *lv2_Plugin  = lv2u_uri(NS_LV2   "Plugin");
    SordNode *ingen_Graph = lv2u_uri(NS_INGEN  "Graph");
    SordNode *ingen_Block = lv2u_uri(NS_INGEN  "Block");
    SordNode *ingen_Arc   = lv2u_uri(NS_INGEN  "Arc");
    SordNode *pedal_PB    = lv2u_uri(NS_PEDAL  "Pedalboard");
    SordNode *ingen_prototype = lv2u_uri(NS_INGEN "prototype");
    SordNode *lv2_prototype   = lv2u_uri(NS_LV2  "prototype");
    SordNode *lv2_port        = lv2u_uri(NS_LV2  "port");
    SordNode *ingen_enabled   = lv2u_uri(NS_INGEN "enabled");
    SordNode *ingen_canvasX   = lv2u_uri(NS_INGEN "canvasX");
    SordNode *ingen_canvasY   = lv2u_uri(NS_INGEN "canvasY");
    SordNode *ingen_value     = lv2u_uri(NS_INGEN "value");
    SordNode *ingen_tail      = lv2u_uri(NS_INGEN "tail");
    SordNode *ingen_head      = lv2u_uri(NS_INGEN "head");
    SordNode *mod_label       = lv2u_uri(NS_MOD   "label");
    SordNode *mod_snapshotable = lv2u_uri(NS_MOD  "snapshotable");
    SordNode *pedal_preset    = lv2u_uri(NS_PEDAL "preset");
    SordNode *doap_name       = lv2u_uri(NS_DOAP  "name");
    SordNode *lv2_ControlPort = lv2u_uri(NS_LV2   "ControlPort");
    SordNode *lv2_InputPort   = lv2u_uri(NS_LV2   "InputPort");

#define ADD(s, p, o)  sord_add(m, (SordQuad){(s),(p),(o),NULL})

    /* Pedalboard subject */
    char pb_uri[PB_PATH_MAX];
    snprintf(pb_uri, sizeof(pb_uri), "file://%s/%s.ttl", pb->path, pb->name);
    SordNode *pb_subj = lv2u_uri(pb_uri);

    ADD(pb_subj, rdf_type, lv2_Plugin);
    ADD(pb_subj, rdf_type, ingen_Graph);
    ADD(pb_subj, rdf_type, pedal_PB);
    ADD(pb_subj, doap_name, lv2u_string(pb->name));

    /* Standard pedalboard ports */
    const char *pb_ports[] = {
        "capture_1", "capture_2", "playback_1", "playback_2", NULL
    };
    for (int pi = 0; pb_ports[pi]; pi++) {
        char port_uri[PB_PATH_MAX];
        snprintf(port_uri, sizeof(port_uri), "%s/%s", pb_uri, pb_ports[pi]);
        SordNode *pn = lv2u_uri(port_uri);
        ADD(pb_subj, lv2_port, pn);
        sord_node_free(lv2u_world(), pn);
    }

    /* Plugin blocks */
    for (int i = 0; i < pb->plugin_count; i++) {
        pb_plugin_t *plug = &pb->plugins[i];

        char block_uri[PB_PATH_MAX];
        snprintf(block_uri, sizeof(block_uri), "%s/%s", pb_uri, plug->symbol);
        SordNode *block = lv2u_uri(block_uri);

        ADD(block, rdf_type,       ingen_Block);
        ADD(block, lv2_prototype,  lv2u_uri(plug->uri));
        ADD(block, ingen_canvasX,  lv2u_float_node(plug->canvas_x));
        ADD(block, ingen_canvasY,  lv2u_float_node(plug->canvas_y));
        ADD(block, ingen_enabled,  lv2u_bool_node(plug->enabled));
        ADD(block, mod_label,      lv2u_string(plug->label));
        ADD(pb_subj, lv2_port, block); /* block listed as port of pedalboard graph */

        if (plug->preset_uri[0])
            ADD(block, pedal_preset, lv2u_uri(plug->preset_uri));

        /* Control ports */
        for (int j = 0; j < plug->port_count; j++) {
            pb_port_t *port = &plug->ports[j];
            char port_uri[PB_PATH_MAX];
            snprintf(port_uri, sizeof(port_uri), "%s/%s/%s",
                     pb_uri, plug->symbol, port->symbol);
            SordNode *pn = lv2u_uri(port_uri);

            ADD(pn, rdf_type, lv2_ControlPort);
            ADD(pn, rdf_type, lv2_InputPort);
            ADD(pn, ingen_value, lv2u_float_node(port->value));
            ADD(pn, mod_snapshotable, lv2u_bool_node(port->snapshotable));
            ADD(block, lv2_port, pn);

            sord_node_free(lv2u_world(), pn);
        }

        sord_node_free(lv2u_world(), block);
    }

    /* Connections (ingen:Arc) */
    for (int i = 0; i < pb->connection_count; i++) {
        pb_connection_t *conn = &pb->connections[i];
        char arc_id[64];
        snprintf(arc_id, sizeof(arc_id), "arc_%d", i);
        SordNode *arc = lv2u_blank(arc_id);
        ADD(arc, rdf_type,    ingen_Arc);
        ADD(arc, ingen_tail,  lv2u_uri(conn->from));
        ADD(arc, ingen_head,  lv2u_uri(conn->to));
        sord_node_free(lv2u_world(), arc);
    }

#undef ADD

    char base[PB_PATH_MAX];
    snprintf(base, sizeof(base), "file://%s", ttl_path);
    int r = lv2u_save_ttl(m, ttl_path, base);
    sord_free(m);
    return r;
}

static int pb_save_manifest(pedalboard_t *pb)
{
    char manifest_path[PB_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.ttl", pb->path);
    FILE *f = fopen(manifest_path, "w");
    if (!f) return -1;

    fprintf(f,
        "@prefix atom:  <http://lv2plug.in/ns/ext/atom#> .\n"
        "@prefix doap:  <http://usefulinc.com/ns/doap#> .\n"
        "@prefix ingen: <http://drobilla.net/ns/ingen#> .\n"
        "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix pedal: <http://moddevices.com/ns/modpedal#> .\n"
        "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "\n"
        "<%s.ttl>\n"
        "    lv2:prototype ingen:GraphPrototype ;\n"
        "    doap:name \"%s\" ;\n"
        "    pedal:screenshot <screenshot.png> ;\n"
        "    pedal:thumbnail <thumbnail.png> ;\n"
        "    ingen:polyphony 1 ;\n"
        "    a lv2:Plugin , ingen:Graph , pedal:Pedalboard .\n",
        pb->name, pb->name);

    fclose(f);
    return 0;
}

/* ─── Save ───────────────────────────────────────────────────────────────────── */

int pb_save(pedalboard_t *pb)
{
    ensure_dir(pb->path);

    /* manifest.ttl */
    if (pb_save_manifest(pb) < 0) return -1;

    /* <name>.ttl */
    char ttl_path[PB_PATH_MAX];
    snprintf(ttl_path, sizeof(ttl_path), "%s/%s.ttl", pb->path, pb->name);
    if (pb_save_ttl(pb, ttl_path) < 0) return -1;

    /* snapshots.json */
    char snap_path[PB_PATH_MAX];
    snprintf(snap_path, sizeof(snap_path), "%s/snapshots.json", pb->path);
    if (snapshot_save(snap_path, pb->snapshots, pb->snapshot_count,
                      pb->current_snapshot) < 0)
        return -1;

    pb->modified = false;
    return 0;
}

int pb_save_as(pedalboard_t *pb, const char *new_dir)
{
    snprintf(pb->path, sizeof(pb->path), "%s", new_dir);
    return pb_save(pb);
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

/* ─── Snapshot wrappers ──────────────────────────────────────────────────────── */

int pb_snapshot_save_current(pedalboard_t *pb, const char *name)
{
    if (pb->snapshot_count >= PB_MAX_SNAPSHOTS) return -1;
    pb_snapshot_t *snap = &pb->snapshots[pb->snapshot_count];
    memset(snap, 0, sizeof(*snap));
    snprintf(snap->name, sizeof(snap->name), "%s", name);

    for (int i = 0; i < pb->plugin_count; i++) {
        pb_plugin_t *plug = &pb->plugins[i];
        snap_plugin_t *sp = &snap->plugins[snap->plugin_count++];
        snprintf(sp->symbol, sizeof(sp->symbol), "%s", plug->symbol);
        sp->bypassed = !plug->enabled;
        snprintf(sp->preset_uri, sizeof(sp->preset_uri), "%s", plug->preset_uri);
        for (int j = 0; j < plug->port_count; j++) {
            if (!plug->ports[j].snapshotable) continue;
            sp->params[sp->param_count].value = plug->ports[j].value;
            snprintf(sp->params[sp->param_count].symbol,
                     sizeof(sp->params[sp->param_count].symbol),
                     "%s", plug->ports[j].symbol);
            sp->param_count++;
        }
    }

    pb->current_snapshot = pb->snapshot_count;
    pb->snapshot_count++;
    pb->modified = true;
    return pb->current_snapshot;
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
        for (int j = 0; j < sp->param_count; j++) {
            for (int k = 0; k < plug->port_count; k++) {
                if (strcmp(plug->ports[k].symbol, sp->params[j].symbol) == 0) {
                    plug->ports[k].value = sp->params[j].value;
                    break;
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
