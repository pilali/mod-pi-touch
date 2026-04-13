#include "pre_fx.h"
#include "host_comm.h"
#include "settings.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>

/* ─── Plugin URIs ─────────────────────────────────────────────────────────────── */
#define GATE_URI  "http://moddevices.com/plugins/mod-devel/System-NoiseGate"
#define TUNER_URI "http://gareus.org/oss/lv2/tuna#mod"

/* ─── Internal state ──────────────────────────────────────────────────────────── */
static bool            g_loaded    = false;
static bool            g_monitoring = false;

static pthread_mutex_t g_tuner_mutex = PTHREAD_MUTEX_INITIALIZER;
static pre_fx_tuner_t  g_tuner       = {0};

/* Polling thread — replaces monitor_output (which only sends one snapshot) */
static pthread_t     g_poll_tid;
static volatile bool g_poll_running = false;

/* ─── Polling thread ──────────────────────────────────────────────────────────── */

static void *tuner_poll_thread(void *arg)
{
    (void)arg;
    while (g_poll_running) {
        if (g_monitoring && g_loaded) {
            float freq = 0.0f;
            if (host_param_get(PRE_FX_TUNER_INSTANCE, "freq_out", &freq) >= 0)
                pre_fx_on_feedback(PRE_FX_TUNER_INSTANCE, "freq_out", freq);
        }
        usleep(100000); /* 100 ms */
    }
    return NULL;
}

/* ─── Internal helpers ────────────────────────────────────────────────────────── */

static void do_load(void)
{
    g_loaded = false;

    /* Explicitly remove previous instances before re-adding.
     * host_remove_all() (-1) may not update mod-host's bookkeeping for
     * non-sequential instance IDs, causing ERR_INSTANCE_ALREADY_EXISTS (-2).
     * Ignore errors here — the instances may or may not exist. */
    host_remove_plugin(PRE_FX_TUNER_INSTANCE);
    host_remove_plugin(PRE_FX_GATE_INSTANCE);

    if (host_add_plugin(PRE_FX_GATE_INSTANCE, GATE_URI) < 0) {
        fprintf(stderr, "[pre_fx] Failed to load noise gate (%s)\n", GATE_URI);
        return;
    }
    if (host_add_plugin(PRE_FX_TUNER_INSTANCE, TUNER_URI) < 0) {
        fprintf(stderr, "[pre_fx] Failed to load tuner (%s)\n", TUNER_URI);
        host_remove_plugin(PRE_FX_GATE_INSTANCE);
        return;
    }

    /* Connect system captures to gate inputs */
    host_connect("system:capture_1", "effect_9993:Input_1");
    host_connect("system:capture_2", "effect_9993:Input_2");

    /* Connect first capture to tuner (parallel, monitoring only — output not used) */
    host_connect("system:capture_1", "effect_9994:in");

    g_loaded = true;

    /* Apply current prefs */
    pre_fx_apply_gate();
    pre_fx_apply_tuner_ref();

    /* If monitoring was active before reload, restart it */
    if (g_monitoring) {
        g_monitoring = false;
        pre_fx_tuner_start_monitoring();
    }

    fprintf(stderr, "[pre_fx] Loaded (gate=%d, tuner=%d)\n",
            PRE_FX_GATE_INSTANCE, PRE_FX_TUNER_INSTANCE);
}

/* ─── Public API ──────────────────────────────────────────────────────────────── */

void pre_fx_init(void)
{
    g_poll_running = true;
    pthread_create(&g_poll_tid, NULL, tuner_poll_thread, NULL);
    do_load();
}

void pre_fx_reload(void)
{
    do_load();
}

void pre_fx_apply_gate(void)
{
    if (!g_loaded) return;
    mpt_settings_t *s = settings_get();

    /* Bypass when disabled so audio still passes through */
    host_bypass(PRE_FX_GATE_INSTANCE, !s->gate_enabled);
    host_param_set(PRE_FX_GATE_INSTANCE, "Threshold", s->gate_threshold);
    host_param_set(PRE_FX_GATE_INSTANCE, "Decay",     s->gate_decay);
    host_param_set(PRE_FX_GATE_INSTANCE, "Gate_Mode", (float)s->gate_mode);
}

void pre_fx_apply_tuner_ref(void)
{
    if (!g_loaded) return;
    mpt_settings_t *s = settings_get();
    /* LV2 port symbol per tuna.ttl index 5 */
    host_param_set(PRE_FX_TUNER_INSTANCE, "tuning", s->tuner_ref_freq);
}

void pre_fx_tuner_start_monitoring(void)
{
    if (!g_loaded || g_monitoring) return;
    g_monitoring = true;
    fprintf(stderr, "[pre_fx] tuner polling started\n");
}

void pre_fx_tuner_stop_monitoring(void)
{
    g_monitoring = false;
}

void pre_fx_on_feedback(int instance, const char *symbol, float value)
{
    (void)instance;
    if (strcmp(symbol, "freq_out") != 0) return;

    pthread_mutex_lock(&g_tuner_mutex);
    g_tuner.freq_hz = value;

    if (value >= 10.0f) {
        /* Derive note / cents / octave from frequency (same logic as mod-ui) */
        mpt_settings_t *s = settings_get();
        float ref = (s->tuner_ref_freq > 0.0f) ? s->tuner_ref_freq : 440.0f;
        float cents_from_a4 = 1200.0f * log2f(value / ref);
        int   semitone      = (int)roundf(cents_from_a4 / 100.0f);
        g_tuner.cent   = cents_from_a4 - (float)semitone * 100.0f;
        /* MIDI note: A4 = 69, note (0=C): midi%12, octave: midi/12 - 1 */
        int midi        = 69 + semitone;
        g_tuner.note   = ((midi % 12) + 12) % 12;
        g_tuner.octave = midi / 12 - 1;
    }
    pthread_mutex_unlock(&g_tuner_mutex);
}

pre_fx_tuner_t pre_fx_get_tuner(void)
{
    pre_fx_tuner_t t;
    pthread_mutex_lock(&g_tuner_mutex);
    t = g_tuner;
    pthread_mutex_unlock(&g_tuner_mutex);
    return t;
}

bool pre_fx_is_loaded(void)
{
    return g_loaded;
}
