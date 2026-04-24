#pragma once
#include <stdbool.h>
#include <stddef.h>

#define HW_MAX_AUDIO_DEVICES 8
#define HW_MAX_MIDI_PORTS    16

/* One ALSA PCM card available for JACK */
typedef struct {
    char alsa_id[16];   /* "hw:0", "hw:1", … */
    char label[64];     /* "pisound", "USB Audio Device", … */
} hw_audio_device_t;

/* One physical MIDI device (one entry per device, both directions merged) */
typedef struct {
    char dev[64];       /* JACK capture port, e.g. "system:midi_capture_2" */
    char dev_out[64];   /* JACK playback port, e.g. "system:midi_playback_2" */
    char label[64];     /* human-readable device name, e.g. "pisound" */
    bool is_input;      /* capture port exists */
    bool is_output;     /* playback port exists */
} hw_midi_port_t;

/* Enumerate ALSA PCM cards from /proc/asound/cards.
 * Returns count written to out[] (≤ max). */
int hw_detect_audio(hw_audio_device_t *out, int max);

/* Enumerate MIDI ports: physical ALSA ports (amidi -l) + JACK software clients
 * (a2jmidid, TouchOSC, etc.). The Midi-Through loopback port is excluded.
 * Returns count written to out[] (≤ max). */
int hw_detect_midi(hw_midi_port_t *out, int max);

/* Detect the ALSA Midi-Through port in JACK (Virtual MIDI Loopback).
 * Fills out_jack_port with the JACK port name (e.g. "system:midi_capture_3").
 * Returns 1 if found, 0 otherwise. */
int hw_detect_midi_loopback(char *out_jack_port, size_t sz);

/* ── JACK port query ─────────────────────────────────────────────────────── */

typedef struct {
    int audio_capture;   /* number of system:capture_N audio ports  */
    int audio_playback;  /* number of system:playback_N audio ports */
} hw_jack_ports_t;

/* Connect to the running JACK server as a temporary client and count
 * system audio ports.  Does NOT start jackd if it is not running.
 * Returns 0 on success, -1 if JACK is unreachable. */
int hw_detect_jack_ports(hw_jack_ports_t *out);
