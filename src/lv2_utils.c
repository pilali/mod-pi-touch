#include "lv2_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Global state ──────────────────────────────────────────────────────────── */

static SordWorld *g_world = NULL;

void lv2u_world_init(void)
{
    if (!g_world) g_world = sord_world_new();
}

void lv2u_world_fini(void)
{
    if (g_world) { sord_world_free(g_world); g_world = NULL; }
}

SordWorld *lv2u_world(void)
{
    return g_world;
}

/* ─── TTL loading ────────────────────────────────────────────────────────────── */

SordModel *lv2u_load_ttl(const char *path)
{
    SordModel *model = sord_new(g_world, SORD_SPO | SORD_OPS, false);
    if (!model) return NULL;

    SerdEnv  *env    = serd_env_new(NULL);
    SerdReader *reader = sord_new_reader(model, env, SERD_TURTLE, NULL);
    if (!reader) {
        serd_env_free(env);
        sord_free(model);
        return NULL;
    }

    /* Build file URI from path */
    char file_uri[1024];
    snprintf(file_uri, sizeof(file_uri), "file://%s", path);
    SerdNode base = serd_node_new_uri_from_string(
                        (const uint8_t *)file_uri, NULL, NULL);
    serd_env_set_base_uri(env, &base);

    SerdStatus st = serd_reader_read_file(reader, (const uint8_t *)path);
    serd_reader_free(reader);
    serd_env_free(env);
    serd_node_free(&base);

    if (st != SERD_SUCCESS && st != SERD_FAILURE) {
        fprintf(stderr, "[lv2_utils] Error reading %s: %d\n", path, st);
        sord_free(model);
        return NULL;
    }
    return model;
}

/* ─── TTL saving ─────────────────────────────────────────────────────────────── */

static size_t file_sink(const void *buf, size_t len, void *stream)
{
    return fwrite(buf, 1, len, (FILE *)stream);
}

int lv2u_save_ttl(SordModel *model, const char *path, const char *base_uri)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    /* Parse the base URI string into a SerdURI struct (required by
     * serd_writer_new) so serd can relativize URIs against the base. */
    SerdURI   base_parsed = SERD_URI_NULL;
    SerdNode  base   = serd_node_new_uri_from_string(
                           (const uint8_t *)base_uri, NULL, &base_parsed);
    SerdEnv  *env    = serd_env_new(&base);
    serd_env_set_base_uri(env, &base);

    /* Register standard prefixes */
    struct { const char *prefix; const char *uri; } prefixes[] = {
        { "rdf",   NS_RDF   },
        { "lv2",   NS_LV2   },
        { "ingen", NS_INGEN },
        { "pedal", NS_PEDAL },
        { "doap",  NS_DOAP  },
        { "mod",   NS_MOD   },
        { "atom",  NS_ATOM  },
        { "midi",  NS_MIDI  },
        { NULL, NULL }
    };
    for (int i = 0; prefixes[i].prefix; i++) {
        SerdNode pn = serd_node_from_string(SERD_URI,
                          (const uint8_t *)prefixes[i].uri);
        SerdNode pp = serd_node_from_string(SERD_LITERAL,
                          (const uint8_t *)prefixes[i].prefix);
        serd_env_set_prefix(env, &pp, &pn);
    }

    SerdWriter *writer = serd_writer_new(
        SERD_TURTLE, SERD_STYLE_ABBREVIATED | SERD_STYLE_CURIED,
        env, &base_parsed, file_sink, f);

    serd_env_foreach(env, (SerdPrefixSink)serd_writer_set_prefix, writer);

    SordIter *it = sord_begin(model);
    while (!sord_iter_end(it)) {
        SordQuad q;
        sord_iter_get(it, q);
        const SerdNode *s  = sord_node_to_serd_node(q[SORD_SUBJECT]);
        const SerdNode *p  = sord_node_to_serd_node(q[SORD_PREDICATE]);
        const SerdNode *o  = sord_node_to_serd_node(q[SORD_OBJECT]);
        /* Pass datatype and language so serd can write typed/lang literals
         * correctly (e.g. abbreviate xsd:decimal to bare 1097.0). */
        const SordNode *dt = sord_node_get_datatype(q[SORD_OBJECT]);
        const char     *lg = sord_node_get_language(q[SORD_OBJECT]);
        const SerdNode *dt_serd = dt ? sord_node_to_serd_node(dt) : NULL;
        SerdNode lang_node;
        const SerdNode *lang_serd = NULL;
        if (lg && lg[0]) {
            lang_node  = serd_node_from_string(SERD_LITERAL, (const uint8_t *)lg);
            lang_serd  = &lang_node;
        }
        serd_writer_write_statement(writer, 0, NULL, s, p, o, dt_serd, lang_serd);
        sord_iter_next(it);
    }
    sord_iter_free(it);

    serd_writer_finish(writer);
    serd_writer_free(writer);
    serd_env_free(env);
    serd_node_free(&base);
    fclose(f);
    return 0;
}

/* ─── Node constructors ──────────────────────────────────────────────────────── */

SordNode *lv2u_uri(const char *uri)
{
    return sord_new_uri(g_world, (const uint8_t *)uri);
}

SordNode *lv2u_curie(const char *prefix_uri, const char *local)
{
    char full[2048];
    snprintf(full, sizeof(full), "%s%s", prefix_uri, local);
    return sord_new_uri(g_world, (const uint8_t *)full);
}

SordNode *lv2u_blank(const char *id)
{
    return sord_new_blank(g_world, (const uint8_t *)id);
}

SordNode *lv2u_string(const char *str)
{
    return sord_new_literal(g_world, NULL, (const uint8_t *)str, NULL);
}

SordNode *lv2u_float_node(float v)
{
    /* Use xsd:decimal so serd abbreviates to bare literals (e.g. 2606.0),
     * matching the format written by mod-ui. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", (double)v);
    /* Ensure at least one decimal point so the literal round-trips as decimal */
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E'))
        strncat(buf, ".0", sizeof(buf) - strlen(buf) - 1);
    SordNode *type = lv2u_uri("http://www.w3.org/2001/XMLSchema#decimal");
    SordNode *n = sord_new_literal(g_world, type, (const uint8_t *)buf, NULL);
    sord_node_free(g_world, type);
    return n;
}

SordNode *lv2u_bool_node(bool v)
{
    SordNode *type = lv2u_uri("http://www.w3.org/2001/XMLSchema#boolean");
    SordNode *n = sord_new_literal(g_world, type,
                                   (const uint8_t *)(v ? "true" : "false"), NULL);
    sord_node_free(g_world, type);
    return n;
}

SordNode *lv2u_int_node(int v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);
    SordNode *type = lv2u_uri("http://www.w3.org/2001/XMLSchema#integer");
    SordNode *n = sord_new_literal(g_world, type, (const uint8_t *)buf, NULL);
    sord_node_free(g_world, type);
    return n;
}

/* ─── Query helpers ──────────────────────────────────────────────────────────── */

SordNode *lv2u_get_object(SordModel *m, SordNode *subject, SordNode *predicate)
{
    SordQuad pat = { subject, predicate, NULL, NULL };
    SordIter *it = sord_find(m, pat);
    if (!it || sord_iter_end(it)) {
        if (it) sord_iter_free(it);
        return NULL;
    }
    SordQuad q;
    sord_iter_get(it, q);
    SordNode *obj = q[SORD_OBJECT]; /* borrowed */
    sord_iter_free(it);
    return obj;
}

bool lv2u_get_float(SordModel *m, SordNode *subject, SordNode *predicate, float *out)
{
    SordNode *obj = lv2u_get_object(m, subject, predicate);
    if (!obj) return false;
    const uint8_t *str = sord_node_get_string(obj);
    if (!str) return false;
    *out = (float)atof((const char *)str);
    return true;
}

bool lv2u_get_bool(SordModel *m, SordNode *subject, SordNode *predicate, bool *out)
{
    SordNode *obj = lv2u_get_object(m, subject, predicate);
    if (!obj) return false;
    const uint8_t *str = sord_node_get_string(obj);
    if (!str) return false;
    *out = (strcmp((const char *)str, "true") == 0);
    return true;
}

bool lv2u_get_string(SordModel *m, SordNode *subject, SordNode *predicate,
                     char *buf, size_t bufsz)
{
    SordNode *obj = lv2u_get_object(m, subject, predicate);
    if (!obj) return false;
    const uint8_t *str = sord_node_get_string(obj);
    if (!str) return false;
    snprintf(buf, bufsz, "%s", (const char *)str);
    return true;
}

SordIter *lv2u_iter_type(SordModel *m, SordNode *type_node)
{
    SordNode *rdf_type = lv2u_uri(NS_RDF "type");
    SordQuad pat = { NULL, rdf_type, type_node, NULL };
    SordIter *it = sord_find(m, pat);
    sord_node_free(g_world, rdf_type);
    return it;
}
