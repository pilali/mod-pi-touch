#pragma once

#include <stdbool.h>

/* Reserved mod-host instance IDs (outside pedalboard range 0..N) */
#define PRE_FX_GATE_INSTANCE  9993
#define PRE_FX_TUNER_INSTANCE 9994

/* Latest tuner readings */
typedef struct {
    int   note;      /* 0-11 (C=0, C#=1, D=2, ..., B=11) */
    float cent;      /* -50..+50 cents */
    float freq_hz;   /* detected frequency in Hz (0 = no signal) */
    int   octave;    /* octave number */
    float rms_db;    /* signal level at tuner input in dBFS (diagnostic) */
} pre_fx_tuner_t;

/* Initialize pre-fx after host connect (tuner_host + gate async). */
void pre_fx_init(void);

/* Shutdown tuner_host (call before exit). */
void pre_fx_fini(void);

/* Re-load gate after host_remove_all() (called after each pedalboard load). */
void pre_fx_reload(void);

/* Apply current gate settings (enabled, threshold, decay, mode) to mod-host. */
void pre_fx_apply_gate(void);

/* Apply current tuner reference frequency to mod-host. */
void pre_fx_apply_tuner_ref(void);

/* Reconnect tuner audio input based on settings.tuner_input (0=both,1=L,2=R). */
void pre_fx_apply_tuner_input(void);

/* Enable real-time monitoring of tuner output ports. */
void pre_fx_tuner_start_monitoring(void);

/* Disable tuner monitoring. */
void pre_fx_tuner_stop_monitoring(void);

/* Called from the feedback thread when param arrives for instances 9993/9994.
 * Thread-safe: updates internal state only, no LVGL calls. */
void pre_fx_on_feedback(int instance, const char *symbol, float value);

/* Thread-safe snapshot of the latest tuner data. */
pre_fx_tuner_t pre_fx_get_tuner(void);

/* True if pre-fx plugins are currently loaded in mod-host. */
bool pre_fx_is_loaded(void);
