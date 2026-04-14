#include "pre_fx.h"
#include "host_comm.h"
#include "settings.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

/* ─── Plugin URIs ─────────────────────────────────────────────────────────────── */
#define GATE_URI  "http://moddevices.com/plugins/mod-devel/System-NoiseGate"
#define TUNER_URI "http://gareus.org/oss/lv2/tuna#mod"

/* ─── Internal state ──────────────────────────────────────────────────────────── */
static bool            g_loaded    = false;
static bool            g_monitoring = false;

static pthread_mutex_t g_tuner_mutex = PTHREAD_MUTEX_INITIALIZER;
static pre_fx_tuner_t  g_tuner       = {0};

/* Re-subscription polling thread — forces a fresh snapshot every 100 ms.
 * This mod-host fork only sends one snapshot per monitor_output call;
 * repeated calls force continuous updates via the feedback socket. */
static pthread_t     g_poll_tid;
static volatile bool g_poll_running = false;


/* ─── Re-subscription polling thread ─────────────────────────────────────────── */

static void *tuner_poll_thread(void *arg)
{
    (void)arg;
    int tick = 0;
    while (g_poll_running) {
        if (g_monitoring && g_loaded) {
            int r = host_monitor_output(PRE_FX_TUNER_INSTANCE, "freq_out");
            if ((tick++ % 10) == 0)  /* log every 1s */
                fprintf(stderr, "[tuner_poll] monitor_output → %d  freq=%.1f\n",
                        r, (double)pre_fx_get_tuner().freq_hz);
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

    /* Connect tuner input(s) — see pre_fx_apply_tuner_input().
     * Also connect tuner's audio out to keep it active in the JACK graph. */
    host_connect("effect_9994:out",  "system:playback_1");
    /* Apply input routing (both captures by default) */
    mpt_settings_t *s = settings_get();
    if (s->tuner_input != 2)
        host_connect("system:capture_1", "effect_9994:in");
    if (s->tuner_input != 1)
        host_connect("system:capture_2", "effect_9994:in");

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

void pre_fx_apply_tuner_input(void)
{
    if (!g_loaded) return;
    /* Disconnect both, then reconnect according to setting */
    host_disconnect("system:capture_1", "effect_9994:in");
    host_disconnect("system:capture_2", "effect_9994:in");
    mpt_settings_t *s = settings_get();
    if (s->tuner_input != 2)
        host_connect("system:capture_1", "effect_9994:in");
    if (s->tuner_input != 1)
        host_connect("system:capture_2", "effect_9994:in");
    fprintf(stderr, "[pre_fx] tuner input → %d\n", s->tuner_input);
}

void pre_fx_tuner_start_monitoring(void)
{
    if (!g_loaded || g_monitoring) return;
    g_monitoring = true; /* poll thread will call monitor_output immediately */
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
