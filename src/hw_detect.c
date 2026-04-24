#define _GNU_SOURCE
#include "hw_detect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* JACK on this build uses /dev/shm as its tmpdir. */
#define JACK_LSP "TMPDIR=/dev/shm jack_lsp"

/* ─── Audio (ALSA PCM cards) ─────────────────────────────────────────────────
 *
 * /proc/asound/cards format (two lines per card):
 *   " N [short_name  ]: driver - Long Card Name"
 *   "                   Long Card Name"
 *
 * We only parse the first line of each card (contains the card number).
 */
int hw_detect_audio(hw_audio_device_t *out, int max)
{
    FILE *f = fopen("/proc/asound/cards", "r");
    if (!f) return 0;

    int  count = 0;
    char line[256];

    while (fgets(line, sizeof(line), f) && count < max) {
        int  card_num;
        char bracket[32] = "";
        char long_name[128] = "";

        if (sscanf(line, " %d [%31[^]]]", &card_num, bracket) < 1)
            continue;

        const char *dash = strstr(line, " - ");
        if (dash) {
            snprintf(long_name, sizeof(long_name), "%s", dash + 3);
            char *end = long_name + strlen(long_name) - 1;
            while (end >= long_name && (*end == '\n' || *end == '\r' || *end == ' '))
                *end-- = '\0';
        }

        snprintf(out[count].alsa_id, sizeof(out[count].alsa_id), "hw:%d", card_num);
        snprintf(out[count].label,   sizeof(out[count].label),
                 "%s", long_name[0] ? long_name : bracket);
        count++;
    }

    fclose(f);
    return count;
}

/* ─── MIDI port detection ────────────────────────────────────────────────────
 *
 * Uses `jack_lsp -p --aliases` (no JACK client API) to enumerate hardware
 * MIDI ports.  Only ports flagged `physical` are included — this matches
 * mod-ui's get_jack_hardware_ports(JackPortIsPhysical) behaviour and
 * naturally excludes plugin ports (effect_N:*), mod-host:*, mod-monitor:*
 * and any other software-only JACK clients.
 *
 * Port direction in JACK vs. user perception:
 *   JACK "output" (sends data) = hardware MIDI source = user's MIDI INPUT
 *   JACK "input"  (accepts data) = hardware MIDI sink  = user's MIDI OUTPUT
 *
 * Ports included:
 *   system:midi_capture_N   (physical, output) → is_input=true
 *   system:midi_playback_N  (physical, input)  → is_output=true
 *   ttymidi:MIDI_in         (physical, output) → is_input=true
 *   ttymidi:MIDI_out        (physical, input)  → is_output=true
 *   … and any other physical MIDI client port
 *
 * Midi-Through loopback is always excluded (handled by hw_detect_midi_loopback).
 * Audio ports (system:capture_N, system:playback_N) are excluded by name prefix.
 *
 * Label derivation:
 *   For system:midi_* ports the first alias is "alsa_pcm:<name>/…"; we extract
 *   <name> (e.g. "pisound", "touchosc").
 *   For other ports (e.g. ttymidi) the port name itself is used.
 */
int hw_detect_midi(hw_midi_port_t *out, int max)
{
    int count = 0;

    FILE *f = popen(JACK_LSP " -p --aliases 2>/dev/null", "r");
    if (!f) return 0;

    /* ── Per-port accumulated state ── */
    char cur_port[128]  = "";
    char cur_label[128] = "";
    bool cur_physical   = false;
    bool cur_is_output  = false; /* JACK "output" direction */
    bool cur_loopback   = false;
    bool cur_is_midi    = false; /* true if not a pure audio port */

    char line[512];

/* Emit cur_* if it is a valid physical MIDI port. */
#define EMIT() do { \
    if (cur_port[0] && cur_physical && cur_is_midi && !cur_loopback \
            && count < max) { \
        snprintf(out[count].dev,   sizeof(out[count].dev),   "%s", cur_port); \
        snprintf(out[count].label, sizeof(out[count].label), "%s", \
                 cur_label[0] ? cur_label : cur_port); \
        out[count].is_input  = cur_is_output;  \
        out[count].is_output = !cur_is_output; \
        count++; \
    } \
} while (0)

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;

        if (line[0] != ' ' && line[0] != '\t') {
            /* ── New port name line: emit previous, reset state ── */
            EMIT();

            snprintf(cur_port, sizeof(cur_port), "%s", line);
            cur_label[0]  = '\0';
            cur_physical  = false;
            cur_is_output = false;
            cur_loopback  = false;

            /* Classify: audio system ports are excluded by name. */
            if (strncmp(line, "system:midi_", 12) == 0)
                cur_is_midi = true;               /* system MIDI port */
            else if (strncmp(line, "system:", 7) == 0)
                cur_is_midi = false;              /* system audio port — skip */
            else
                cur_is_midi = true;               /* non-system port (ttymidi etc.) */

        } else if (line[0] == ' ') {
            /* ── Alias line (leading spaces) ── */
            const char *a = line + strspn(line, " ");

            /* Flag loopback */
            if (strcasestr(a, "Midi-Through") || strcasestr(a, "midi_through"))
                cur_loopback = true;

            /* Build label from first alias only */
            if (!cur_label[0]) {
                if (strncmp(a, "alsa_pcm:", 9) == 0) {
                    /* "alsa_pcm:pisound/midi_playback_1" → "pisound" */
                    const char *name  = a + 9;
                    const char *slash = strchr(name, '/');
                    if (slash)
                        snprintf(cur_label, sizeof(cur_label),
                                 "%.*s", (int)(slash - name), name);
                    else
                        snprintf(cur_label, sizeof(cur_label), "%s", name);
                }
                /* else: leave cur_label empty; port name used as fallback */
            }

        } else {
            /* ── Properties line (leading tab): "\tproperties: output,physical,…" ── */
            const char *p = line + 1; /* skip tab */
            if (strstr(p, "physical")) cur_physical  = true;
            if (strstr(p, "output"))   cur_is_output = true;
        }
    }
    EMIT(); /* emit last port */

#undef EMIT

    pclose(f);
    return count;
}

/* ─── MIDI loopback detection ───────────────────────────────────────────────
 *
 * Uses jack_lsp --aliases to find the system:midi_capture_N port whose alias
 * contains "Midi-Through".  Returns that port name so the caller can derive
 * the matching playback port.
 */
int hw_detect_midi_loopback(char *out_jack_port, size_t sz)
{
    out_jack_port[0] = '\0';

    FILE *f = popen(JACK_LSP " --aliases 2>/dev/null", "r");
    if (!f) return 0;

    char cur[128] = "";
    char line[512];
    int  found = 0;

    while (fgets(line, sizeof(line), f) && !found) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] != ' ' && line[0] != '\t') {
            if (strncmp(line, "system:midi_capture_", 20) == 0)
                snprintf(cur, sizeof(cur), "%s", line);
            else
                cur[0] = '\0';
        } else if (cur[0]) {
            const char *a = line + strspn(line, " \t");
            if (strcasestr(a, "Midi-Through") || strcasestr(a, "midi_through")) {
                snprintf(out_jack_port, sz, "%s", cur);
                found = 1;
            }
        }
    }

    pclose(f);
    return found;
}

/* ─── JACK port enumeration ──────────────────────────────────────────────────
 *
 * Uses jack_lsp to count system:capture_N and system:playback_N audio ports.
 */
int hw_detect_jack_ports(hw_jack_ports_t *out)
{
    memset(out, 0, sizeof(*out));

    FILE *f = popen(JACK_LSP " 2>/dev/null", "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "system:capture_",  15) == 0) out->audio_capture++;
        if (strncmp(line, "system:playback_", 16) == 0) out->audio_playback++;
    }

    pclose(f);
    return 0;
}
