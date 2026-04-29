#include "pre_fx.h"
#include "tuner_host.h"
#include "host_comm.h"
#include "settings.h"

#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

/* ─── Plugin URI ──────────────────────────────────────────────────────────────── */
#define GATE_URI "http://moddevices.com/plugins/mod-devel/System-NoiseGate"

/* ─── Internal state (noise gate only) ───────────────────────────────────────── */
static bool            g_loaded     = false;
static pthread_mutex_t g_load_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─── Async gate load thread ─────────────────────────────────────────────────── */

static void do_load(void)
{
    pthread_mutex_lock(&g_load_mutex);
    g_loaded = false;

    /* Remove previous gate instance (ignore errors — may not exist) */
    host_remove_plugin(PRE_FX_GATE_INSTANCE);

    if (host_add_plugin(PRE_FX_GATE_INSTANCE, GATE_URI) < 0) {
        fprintf(stderr, "[pre_fx] Failed to load noise gate (%s)\n", GATE_URI);
        pthread_mutex_unlock(&g_load_mutex);
        return;
    }

    /* Connect system captures to gate inputs */
    host_connect("system:capture_1", "effect_9993:Input_1");
    host_connect("system:capture_2", "effect_9993:Input_2");

    g_loaded = true;
    pre_fx_apply_gate();
    fprintf(stderr, "[pre_fx] Gate loaded (instance %d)\n", PRE_FX_GATE_INSTANCE);
    pthread_mutex_unlock(&g_load_mutex);
}

/* ─── Tuner init thread (large stack for FFTW planner) ──────────────────────── */

static void *tuner_init_thread(void *arg)
{
    (void)arg;
    if (tuner_host_init() < 0)
        fprintf(stderr, "[pre_fx] Tuner host init failed\n");
    else
        pre_fx_apply_tuner_ref();
    return NULL;
}

/* ─── Public API ──────────────────────────────────────────────────────────────── */

void pre_fx_init(void)
{
    /* Tuner only — gate is loaded by pre_fx_reload() which is called from
     * ui_pedalboard_load().  Starting the gate here would race with the
     * pedalboard load's host_remove_all() + pre_fx_reload() sequence. */

    /* Tuner: direct JACK + embedded LV2 host.
     * tuna's activate() calls FFTW's planner which recurses deeply and
     * overflows the default 8 MB stack.  Run init in a thread with 64 MB. */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024 * 1024); /* 64 MB — tuna FFTW planner */
    pthread_t tuner_tid;
    pthread_create(&tuner_tid, &attr, tuner_init_thread, NULL);
    pthread_attr_destroy(&attr);
    pthread_detach(tuner_tid);
}

void pre_fx_fini(void)
{
    tuner_host_fini();
}

void pre_fx_reload(void)
{
    do_load();
}

void pre_fx_apply_gate(void)
{
    if (!g_loaded) return;
    mpt_settings_t *s = settings_get();
    host_bypass(PRE_FX_GATE_INSTANCE, !s->gate_enabled);
    host_param_set(PRE_FX_GATE_INSTANCE, "Threshold", s->gate_threshold);
    host_param_set(PRE_FX_GATE_INSTANCE, "Decay",     s->gate_decay);
    host_param_set(PRE_FX_GATE_INSTANCE, "Gate_Mode", (float)s->gate_mode);
}

void pre_fx_apply_tuner_ref(void)
{
    mpt_settings_t *s = settings_get();
    float ref = (s->tuner_ref_freq > 0.0f) ? s->tuner_ref_freq : 440.0f;
    tuner_host_set_ref_freq(ref);
    tuner_host_set_threshold(-85.0f);
}

void pre_fx_apply_tuner_input(void)
{
    /* tuner_host always reads capture_1 for now;
     * extend tuner_host_set_input() here if stereo/mono selection needed. */
    fprintf(stderr, "[pre_fx] tuner input change — not yet wired to tuner_host\n");
}

/* Monitoring is continuous in tuner_host — these are now no-ops kept for
 * compatibility with ui_pre_fx.c call sites. */
void pre_fx_tuner_start_monitoring(void) {}
void pre_fx_tuner_stop_monitoring(void)  {}

/* Feedback from mod-host — only gate feedback remains (no tuner instance) */
void pre_fx_on_feedback(int instance, const char *symbol, float value)
{
    (void)instance; (void)symbol; (void)value;
    /* Gate has no monitored outputs; nothing to do. */
}

pre_fx_tuner_t pre_fx_get_tuner(void)
{
    tuner_result_t r = tuner_host_get();
    pre_fx_tuner_t t;
    t.freq_hz = r.freq_hz;
    t.rms_db  = r.rms_db;
    t.note    = r.note;
    t.cent    = r.cent;
    t.octave  = r.octave;
    return t;
}

bool pre_fx_is_loaded(void)
{
    return g_loaded;
}
