/* tuner_host.c — Embedded LV2 host for tuna#mod, connected directly to JACK.
 *
 * Architecture:
 *   system:capture_1 ──► [JACK client port] ──► tuna LV2 ──► freq_out / note / cent
 *
 * Only urid:map is required by tuna; we provide a minimal implementation.
 * Atom ports get empty-sequence buffers; audio out goes to a scratch buffer.
 */

#include "tuner_host.h"
#include "pre_fx.h"       /* PRE_FX_TUNER_INSTANCE — kept for reference only */
#include "settings.h"

#include <jack/jack.h>
#include <lilv/lilv.h>
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ─── Tuna port indices (from tuna.ttl) ─────────────────────────────────────── */
#define PORT_CONTROL          0   /* Atom+In  */
#define PORT_NOTIFY           1   /* Atom+Out */
#define PORT_IN               2   /* Audio+In */
#define PORT_OUT              3   /* Audio+Out */
#define PORT_MODE             4   /* Control+In  */
#define PORT_TUNING           5   /* Control+In  — reference freq (Hz) */
#define PORT_RMS              6   /* Control+Out — signal level (dBFS) */
#define PORT_FREQ_OUT         7   /* Control+Out — detected frequency */
#define PORT_OCTAVE           8   /* Control+Out */
#define PORT_NOTE             9   /* Control+Out — 0=C … 11=B */
#define PORT_CENT             10  /* Control+Out — -50..+50 */
#define PORT_THRESHOLD_RMS    13  /* Control+In  */
#define TUNA_NUM_PORTS        20

/* ─── Minimal URID map ───────────────────────────────────────────────────────── */

typedef struct { char **uris; uint32_t count, cap; } UridMap;

static UridMap g_umap;

static LV2_URID umap_map(LV2_URID_Map_Handle h, const char *uri)
{
    UridMap *m = (UridMap *)h;
    for (uint32_t i = 0; i < m->count; i++)
        if (!strcmp(m->uris[i], uri)) return i + 1;
    if (m->count == m->cap) {
        m->cap  = m->cap ? m->cap * 2 : 64;
        m->uris = realloc(m->uris, m->cap * sizeof(char *));
    }
    m->uris[m->count] = strdup(uri);
    return ++m->count;   /* 1-based */
}

/* ─── Plugin instance state ──────────────────────────────────────────────────── */

/* Empty Atom Sequence for the control input port */
typedef struct { LV2_Atom atom; LV2_Atom_Sequence_Body body; } EmptySeq;

static LilvWorld           *g_lworld  = NULL;
static LilvInstance        *g_inst    = NULL;   /* lilv wrapper */
static LV2_Handle           g_handle  = NULL;   /* actual plugin instance (Tuna*) */
static const LV2_Descriptor *g_desc   = NULL;

/* Buffers for each port */
static float   g_ctrl_buf[TUNA_NUM_PORTS];     /* control in/out values */
static float   g_audio_out[8192];              /* scratch audio output */
static EmptySeq g_atom_in;                     /* empty sequence for control input */
static uint8_t  g_atom_out[8192];              /* buffer for notify output (TTL: rsz:minimumSize 4352) */

/* JACK */
static jack_client_t *g_jack      = NULL;
static jack_port_t   *g_jack_in   = NULL;
static bool           g_running   = false;

/* Result — written in JACK rt thread, read from UI thread */
static pthread_mutex_t g_mutex    = PTHREAD_MUTEX_INITIALIZER;
static tuner_result_t  g_result   = {0};

/* ─── JACK process callback (real-time) ──────────────────────────────────────── */

static int jack_process(jack_nframes_t nframes, void *arg)
{
    (void)arg;
    if (!g_handle || !g_desc) return 0;

    /* Connect audio input port to tuna's in */
    float *audio_in = jack_port_get_buffer(g_jack_in, nframes);
    g_desc->connect_port(g_handle, PORT_IN, audio_in);

    /* Audio output → scratch buffer (resize if needed; nframes ≤ 8192) */
    if (nframes <= 8192)
        g_desc->connect_port(g_handle, PORT_OUT, g_audio_out);

    /* Run */
    g_desc->run(g_handle, nframes);

    /* Read output control ports */
    float freq  = g_ctrl_buf[PORT_FREQ_OUT];
    float rms   = g_ctrl_buf[PORT_RMS];
    float cent  = g_ctrl_buf[PORT_CENT];
    float oct   = g_ctrl_buf[PORT_OCTAVE];
    float note  = g_ctrl_buf[PORT_NOTE];

    pthread_mutex_lock(&g_mutex);
    g_result.freq_hz = freq;
    g_result.rms_db  = rms;
    g_result.cent    = cent;
    g_result.octave  = (int)roundf(oct);
    g_result.note    = (int)roundf(note);
    pthread_mutex_unlock(&g_mutex);

    return 0;
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

int tuner_host_init(void)
{
    /* ── URID map ── */
    LV2_URID_Map map_data = { &g_umap, umap_map };

    /* ── LV2 Options — tuna uses maxBlockLength to size its ring buffer.
     * Without this option the plugin may crash in run() because its internal
     * ring buffer is not allocated.  Provide the JACK buffer size (128) and a
     * sequence size large enough for any atom events. ── */
    /* URIDs must be mapped before building the options array */
    LV2_URID urid_maxblk  = umap_map(&g_umap, LV2_BUF_SIZE__maxBlockLength);
    LV2_URID urid_minblk  = umap_map(&g_umap, LV2_BUF_SIZE__minBlockLength);
    LV2_URID urid_seqsize = umap_map(&g_umap, LV2_BUF_SIZE__sequenceSize);
    LV2_URID urid_int     = umap_map(&g_umap, LV2_ATOM__Int);

    static int32_t opt_maxblk  = 128;
    static int32_t opt_minblk  = 32;
    static int32_t opt_seqsize = 8192;

    static LV2_Options_Option options[] = {
        { LV2_OPTIONS_INSTANCE, 0, 0, sizeof(int32_t), 0, NULL },
        { LV2_OPTIONS_INSTANCE, 0, 0, sizeof(int32_t), 0, NULL },
        { LV2_OPTIONS_INSTANCE, 0, 0, sizeof(int32_t), 0, NULL },
        { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, NULL }  /* terminator */
    };
    /* Fill in at runtime so URIDs are resolved */
    options[0].key = urid_maxblk;  options[0].type = urid_int; options[0].value = &opt_maxblk;
    options[1].key = urid_minblk;  options[1].type = urid_int; options[1].value = &opt_minblk;
    options[2].key = urid_seqsize; options[2].type = urid_int; options[2].value = &opt_seqsize;

    /* ── Lilv world ── */
    g_lworld = lilv_world_new();
    lilv_world_load_all(g_lworld);

    LilvNode *tuna_uri = lilv_new_uri(g_lworld,
                                      "http://gareus.org/oss/lv2/tuna#mod");
    const LilvPlugins *plugins = lilv_world_get_all_plugins(g_lworld);
    const LilvPlugin  *plug    = lilv_plugins_get_by_uri(plugins, tuna_uri);
    lilv_node_free(tuna_uri);

    if (!plug) {
        fprintf(stderr, "[tuner_host] tuna#mod not found in LV2 path\n");
        lilv_world_free(g_lworld);
        g_lworld = NULL;
        return -1;
    }

    /* ── JACK — open first so we can pass the real buffer size to tuna ── */
    setenv("JACK_PROMISCUOUS_SERVER", "jack", 0);
    jack_status_t jstatus;
    g_jack = jack_client_open("mod-pi-tuner", JackNoStartServer, &jstatus);
    if (!g_jack) {
        fprintf(stderr, "[tuner_host] Cannot connect to JACK (status 0x%x)\n",
                jstatus);
        lilv_world_free(g_lworld);
        g_lworld = NULL;
        return -1;
    }

    jack_nframes_t rate     = jack_get_sample_rate(g_jack);
    jack_nframes_t buf_size = jack_get_buffer_size(g_jack);

    /* Update options with actual JACK buffer size before instantiation */
    opt_maxblk = (int32_t)buf_size;
    opt_minblk = (int32_t)buf_size;

    LV2_Feature map_feature  = { LV2_URID__map,        &map_data };
    LV2_Feature opts_feature = { LV2_OPTIONS__options,  options   };
    const LV2_Feature *features[] = { &map_feature, &opts_feature, NULL };

    /* ── Instantiate tuna ── */
    g_inst = lilv_plugin_instantiate(plug, (double)rate, features);
    if (!g_inst) {
        fprintf(stderr, "[tuner_host] Failed to instantiate tuna\n");
        jack_client_close(g_jack);
        g_jack = NULL;
        lilv_world_free(g_lworld);
        g_lworld = NULL;
        return -1;
    }

    /* Get the real LV2_Handle (Tuna*) — distinct from the LilvInstance* wrapper.
     * All g_desc->xxx() calls must use g_handle, not g_inst. */
    g_handle = lilv_instance_get_handle(g_inst);
    g_desc   = lilv_instance_get_descriptor(g_inst);

    /* ── Wire up all ports ── */

    /* Control input defaults */
    memset(g_ctrl_buf, 0, sizeof(g_ctrl_buf));
    g_ctrl_buf[PORT_TUNING]        = 440.0f;
    g_ctrl_buf[PORT_THRESHOLD_RMS] = -85.0f;
    g_ctrl_buf[PORT_MODE]          = 0.0f;  /* chromatic */

    for (int i = 0; i < TUNA_NUM_PORTS; i++)
        g_desc->connect_port(g_handle, (uint32_t)i, &g_ctrl_buf[i]);

    /* Atom control input — empty sequence */
    uint32_t seq_type = umap_map(&g_umap, LV2_ATOM__Sequence);
    g_atom_in.atom.type = seq_type;
    g_atom_in.atom.size = sizeof(LV2_Atom_Sequence_Body);
    g_atom_in.body.unit = 0;
    g_atom_in.body.pad  = 0;
    g_desc->connect_port(g_handle, PORT_CONTROL, &g_atom_in);

    /* Atom notify output — scratch buffer with header */
    LV2_Atom *out_atom = (LV2_Atom *)g_atom_out;
    out_atom->type = seq_type;
    out_atom->size = sizeof(g_atom_out) - sizeof(LV2_Atom);
    g_desc->connect_port(g_handle, PORT_NOTIFY, g_atom_out);

    /* Audio out → pre-allocated scratch buffer */
    if (buf_size <= 8192)
        g_desc->connect_port(g_handle, PORT_OUT, g_audio_out);

    /* ── Activate ── */
    lilv_instance_activate(g_inst);

    /* ── Pre-warm: one silent block to initialise tuna's internal state
     * (ring buffer, DLL) before the JACK callback starts firing. ── */
    {
        float dummy_in[128]  = {0};
        float dummy_out[128] = {0};
        uint32_t warm = (buf_size <= 128) ? buf_size : 128;
        g_desc->connect_port(g_handle, PORT_IN,  dummy_in);
        g_desc->connect_port(g_handle, PORT_OUT, dummy_out);
        g_desc->run(g_handle, warm);
        g_desc->connect_port(g_handle, PORT_OUT, g_audio_out);
    }

    /* ── Register JACK port and process callback ── */
    g_jack_in = jack_port_register(g_jack, "in",
                                   JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    if (!g_jack_in) {
        fprintf(stderr, "[tuner_host] Cannot register JACK port\n");
        lilv_instance_deactivate(g_inst);
        lilv_instance_free(g_inst);
        g_inst = NULL;
        jack_client_close(g_jack);
        g_jack = NULL;
        lilv_world_free(g_lworld);
        g_lworld = NULL;
        return -1;
    }

    jack_set_process_callback(g_jack, jack_process, NULL);

    if (jack_activate(g_jack) != 0) {
        fprintf(stderr, "[tuner_host] Cannot activate JACK client\n");
        jack_client_close(g_jack);
        g_jack = NULL;
        lilv_instance_deactivate(g_inst);
        lilv_instance_free(g_inst);
        g_inst = NULL;
        lilv_world_free(g_lworld);
        g_lworld = NULL;
        return -1;
    }

    /* ── Connect system:capture_1 → our input port ── */
    const char *our_port = jack_port_name(g_jack_in);
    if (jack_connect(g_jack, "system:capture_1", our_port) != 0)
        fprintf(stderr, "[tuner_host] Warning: cannot connect system:capture_1\n");

    g_running = true;
    fprintf(stderr, "[tuner_host] Running — JACK %u Hz, buf %u, tuna ok\n",
            rate, buf_size);
    return 0;
}

void tuner_host_fini(void)
{
    g_running = false;
    if (g_jack) {
        jack_deactivate(g_jack);
        jack_client_close(g_jack);
        g_jack = NULL;
    }
    if (g_inst) {
        lilv_instance_deactivate(g_inst);
        lilv_instance_free(g_inst);
        g_inst = NULL;
    }
    if (g_lworld) {
        lilv_world_free(g_lworld);
        g_lworld = NULL;
    }
    for (uint32_t i = 0; i < g_umap.count; i++)
        free(g_umap.uris[i]);
    free(g_umap.uris);
    memset(&g_umap, 0, sizeof(g_umap));
}

void tuner_host_set_ref_freq(float hz)
{
    /* Written from UI thread; read in JACK rt thread.
     * float write is atomic on ARM — no mutex needed. */
    g_ctrl_buf[PORT_TUNING] = hz;
}

void tuner_host_set_threshold(float dbfs)
{
    g_ctrl_buf[PORT_THRESHOLD_RMS] = dbfs;
}

tuner_result_t tuner_host_get(void)
{
    tuner_result_t r;
    pthread_mutex_lock(&g_mutex);
    r = g_result;
    pthread_mutex_unlock(&g_mutex);
    return r;
}

bool tuner_host_is_running(void)
{
    return g_running;
}
