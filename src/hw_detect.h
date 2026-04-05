#pragma once
#include <stdbool.h>

#define HW_MAX_AUDIO_DEVICES 8
#define HW_MAX_MIDI_PORTS    16

/* One ALSA PCM card available for JACK */
typedef struct {
    char alsa_id[16];   /* "hw:0", "hw:1", … */
    char label[64];     /* "pisound", "USB Audio Device", … */
} hw_audio_device_t;

/* One ALSA raw-MIDI port */
typedef struct {
    char dev[64];       /* "/dev/snd/midiC1D0" or "hw:1,0,0" */
    char label[64];     /* human-readable name */
    bool is_input;
    bool is_output;
} hw_midi_port_t;

/* Enumerate ALSA PCM cards from /proc/asound/cards.
 * Returns count written to out[] (≤ max). */
int hw_detect_audio(hw_audio_device_t *out, int max);

/* Enumerate ALSA raw-MIDI ports via `amidi -l`.
 * Returns count written to out[] (≤ max). */
int hw_detect_midi(hw_midi_port_t *out, int max);

/* ── JACK port query ─────────────────────────────────────────────────────── */

typedef struct {
    int audio_capture;   /* number of system:capture_N audio ports  */
    int audio_playback;  /* number of system:playback_N audio ports */
} hw_jack_ports_t;

/* Connect to the running JACK server as a temporary client and count
 * system audio ports.  Does NOT start jackd if it is not running.
 * Returns 0 on success, -1 if JACK is unreachable. */
int hw_detect_jack_ports(hw_jack_ports_t *out);
