#pragma once

/*
 * lv2_utils — helpers for reading/writing mod-ui compatible TTL pedalboard files
 * using serd (serializer/deserializer) and sord (RDF quad store).
 */

#include <stdbool.h>
#include <stddef.h>
#include <serd/serd.h>
#include <sord/sord.h>

/* ─── Namespace URIs (mod-ui compatible) ───────────────────────────────────── */
#define NS_RDF   "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define NS_LV2   "http://lv2plug.in/ns/lv2core#"
#define NS_INGEN "http://drobilla.net/ns/ingen#"
#define NS_PEDAL "http://moddevices.com/ns/modpedal#"
#define NS_DOAP  "http://usefulinc.com/ns/doap#"
#define NS_MOD   "http://moddevices.com/ns/mod#"
#define NS_ATOM  "http://lv2plug.in/ns/ext/atom#"
#define NS_MIDI  "http://lv2plug.in/ns/ext/midi#"
#define NS_PERF  "http://moddevices.com/ns/modpedal#performance#"

/* ─── Global sord world (shared across the app) ────────────────────────────── */

/* Initialize/destroy the global RDF world. Call once at startup/shutdown. */
void lv2u_world_init(void);
void lv2u_world_fini(void);

SordWorld *lv2u_world(void);

/* ─── TTL file operations ────────────────────────────────────────────────────── */

/* Load a TTL file into a new sord model. Returns NULL on error.
 * Caller must call sord_free() when done. */
SordModel *lv2u_load_ttl(const char *path);

/* Write a sord model to a TTL file.
 * base_uri is the document base (e.g. "file:///path/to/pedalboard.ttl").
 * Returns 0 on success. */
int lv2u_save_ttl(SordModel *model, const char *path, const char *base_uri);

/* ─── Convenience node constructors ─────────────────────────────────────────── */

/* These return interned nodes — do NOT free them individually.
 * They are valid until lv2u_world_fini() is called. */
SordNode *lv2u_uri(const char *uri);
SordNode *lv2u_curie(const char *prefix_uri, const char *local);
SordNode *lv2u_blank(const char *id);          /* blank node */
SordNode *lv2u_string(const char *str);        /* xsd:string literal */
SordNode *lv2u_float_node(float v);            /* xsd:decimal literal (bare 2606.0) */
SordNode *lv2u_bool_node(bool v);              /* "true"/"false" literal */
SordNode *lv2u_int_node(int v);                /* xsd:integer literal */

/* ─── Model query helpers ──────────────────────────────────────────────────── */

/* Get the single object matching (subject, predicate, *).
 * Returns a borrowed node (valid until the model is freed), or NULL. */
SordNode *lv2u_get_object(SordModel *m, SordNode *subject, SordNode *predicate);

/* Get a float literal from (subject, predicate, *). Returns false on failure. */
bool lv2u_get_float(SordModel *m, SordNode *subject, SordNode *predicate, float *out);

/* Get a bool literal from (subject, predicate, *). Returns false on failure. */
bool lv2u_get_bool(SordModel *m, SordNode *subject, SordNode *predicate, bool *out);

/* Get a string literal or URI from (subject, predicate, *).
 * Writes into buf. Returns false on failure. */
bool lv2u_get_string(SordModel *m, SordNode *subject, SordNode *predicate,
                     char *buf, size_t bufsz);

/* Iterate all subjects with a given rdf:type. Caller iterates with sord_iter_next(). */
SordIter *lv2u_iter_type(SordModel *m, SordNode *type_node);
