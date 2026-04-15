#pragma once
#include <stdbool.h>

/* Embedded LV2 host for the tuna#mod tuner plugin.
 * Connects directly to JACK (capture_1) — independent of mod-host.
 * All public functions are thread-safe. */

typedef struct {
    float freq_hz;   /* detected frequency, Hz (0 = no signal / below threshold) */
    float rms_db;    /* signal RMS level, dBFS */
    int   note;      /* 0=C … 11=B */
    float cent;      /* cents from nearest note, -50..+50 */
    int   octave;    /* octave number */
} tuner_result_t;

/* Start the JACK client and instantiate tuna. Returns 0 on success. */
int  tuner_host_init(void);

/* Stop JACK client and free resources. */
void tuner_host_fini(void);

/* Set the reference frequency (default 440 Hz). Thread-safe. */
void tuner_host_set_ref_freq(float hz);

/* Set detection threshold in dBFS (default -85). Thread-safe. */
void tuner_host_set_threshold(float dbfs);

/* Read latest tuner result. Thread-safe. */
tuner_result_t tuner_host_get(void);

/* True once the JACK client is active and tuna is running. */
bool tuner_host_is_running(void);
