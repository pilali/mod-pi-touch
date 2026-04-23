#define _GNU_SOURCE
#include "hw_detect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <jack/jack.h>

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

        /* Match lines that start with a card index */
        if (sscanf(line, " %d [%31[^]]]", &card_num, bracket) < 1)
            continue;

        /* Try to extract the long name after " - " */
        const char *dash = strstr(line, " - ");
        if (dash) {
            snprintf(long_name, sizeof(long_name), "%s", dash + 3);
            /* Trim trailing whitespace / newline */
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

/* ─── MIDI port helpers ──────────────────────────────────────────────────────── */

/* Build a human-readable label from a JACK MIDI port name.
 *
 * a2jmidid format: "a2j:Client Name [N] (dir): Port Name"
 *   → extract the port name after "): "
 *
 * Direct JACK client: "ClientName:port_name"
 *   → "ClientName: port_name"
 */
static void jack_midi_port_label(const char *name, char *label, size_t sz)
{
    /* a2j ports: take everything after "): " */
    if (strncmp(name, "a2j:", 4) == 0) {
        const char *sep = strstr(name, "): ");
        if (sep) {
            snprintf(label, sz, "%s", sep + 3);
            return;
        }
    }
    /* Generic: "client:port" → "client: port" */
    const char *colon = strchr(name, ':');
    if (colon && colon[1]) {
        snprintf(label, sz, "%.*s: %s", (int)(colon - name), name, colon + 1);
    } else {
        snprintf(label, sz, "%s", name);
    }
}

/* ─── MIDI (ALSA hardware + JACK software clients) ───────────────────────────
 *
 * Phase 1: physical ALSA rawmidi ports via `amidi -l` (pisound, USB MIDI…).
 *          The Midi-Through port is excluded (it is the loopback, handled
 *          separately by hw_detect_midi_loopback()).
 *
 * Phase 2: JACK MIDI output ports that are NOT system:* ports (those are
 *          already covered by Phase 1 via ALSA aliases).  This catches
 *          software clients bridged by a2jmidid (TouchOSC, LMMS, etc.) and
 *          any app that connects directly as a JACK MIDI client.
 */
int hw_detect_midi(hw_midi_port_t *out, int max)
{
    int count = 0;

    /* ── Phase 1: ALSA hardware rawmidi (amidi -l) ── */
    FILE *f = popen("amidi -l 2>/dev/null", "r");
    if (f) {
        char line[256];
        /* Skip header */
        if (fgets(line, sizeof(line), f)) {
            while (fgets(line, sizeof(line), f) && count < max) {
                char mode[4] = "", dev[64] = "", label[128] = "";
                if (sscanf(line, " %3s %63s %127[^\n]", mode, dev, label) < 2)
                    continue;
                if (!dev[0]) continue;

                /* Trim trailing whitespace */
                if (label[0]) {
                    char *end = label + strlen(label) - 1;
                    while (end >= label && (*end == ' ' || *end == '\r'))
                        *end-- = '\0';
                }

                /* Skip Midi-Through loopback port */
                if (strcasestr(label, "midi through") ||
                    strcasestr(label, "midi-through") ||
                    strcasestr(label, "midi_through"))
                    continue;

                snprintf(out[count].dev,   sizeof(out[count].dev),   "%s", dev);
                snprintf(out[count].label, sizeof(out[count].label), "%s",
                         label[0] ? label : dev);
                out[count].is_input  = (strchr(mode, 'I') != NULL);
                out[count].is_output = (strchr(mode, 'O') != NULL);
                count++;
            }
        }
        pclose(f);
    }

    /* ── Phase 2: JACK software MIDI clients ── */
    jack_status_t status;
    jack_client_t *client = jack_client_open("mpt_midi_scan",
                                             JackNoStartServer, &status);
    if (client) {
        /* JackPortIsOutput = port sends MIDI out (= MIDI source = capture) */
        const char **ports = jack_get_ports(client, NULL,
                                            JACK_DEFAULT_MIDI_TYPE,
                                            JackPortIsOutput);
        if (ports) {
            for (int i = 0; ports[i] && count < max; i++) {
                const char *name = ports[i];

                /* Skip system:* (hardware, already in Phase 1) */
                if (strncmp(name, "system:", 7) == 0) continue;
                /* Skip mod-host and internal monitor ports */
                if (strncmp(name, "mod-host",    8) == 0) continue;
                if (strncmp(name, "mod-monitor", 11) == 0) continue;
                if (strncmp(name, "mpt_",         4) == 0) continue;

                /* Skip Midi-Through loopback (identified by JACK alias) */
                char aliases_buf[2][256];
                char *alias_ptrs[2] = { aliases_buf[0], aliases_buf[1] };
                int n_al = jack_port_get_aliases(
                    jack_port_by_name(client, name),
                    alias_ptrs);
                bool is_loopback = false;
                for (int a = 0; a < n_al; a++) {
                    if (strstr(alias_ptrs[a], "Midi-Through") ||
                        strstr(alias_ptrs[a], "midi_through") ||
                        strstr(alias_ptrs[a], "Midi_Through"))
                        is_loopback = true;
                }
                if (is_loopback) continue;

                char label[64];
                jack_midi_port_label(name, label, sizeof(label));

                snprintf(out[count].dev,   sizeof(out[count].dev),   "%s", name);
                snprintf(out[count].label, sizeof(out[count].label), "%s", label);
                out[count].is_input  = true;
                out[count].is_output = false;
                count++;
            }
            jack_free(ports);
        }
        jack_client_close(client);
    }

    return count;
}

/* ─── MIDI loopback detection (ALSA Midi-Through in JACK) ───────────────────── */

int hw_detect_midi_loopback(char *out_jack_port, size_t sz)
{
    out_jack_port[0] = '\0';

    jack_status_t status;
    jack_client_t *client = jack_client_open("mpt_lb_scan",
                                             JackNoStartServer, &status);
    if (!client) return 0;

    int found = 0;
    const char **ports = jack_get_ports(client, "^system:midi_capture_",
                                        JACK_DEFAULT_MIDI_TYPE,
                                        JackPortIsOutput);
    if (ports) {
        for (int i = 0; ports[i] && !found; i++) {
            char aliases_buf[2][256];
            char *alias_ptrs[2] = { aliases_buf[0], aliases_buf[1] };
            jack_port_t *jp = jack_port_by_name(client, ports[i]);
            int n_al = jack_port_get_aliases(jp, alias_ptrs);
            for (int a = 0; a < n_al; a++) {
                if (strstr(alias_ptrs[a], "Midi-Through") ||
                    strstr(alias_ptrs[a], "midi_through") ||
                    strstr(alias_ptrs[a], "Midi_Through")) {
                    snprintf(out_jack_port, sz, "%s", ports[i]);
                    found = 1;
                    break;
                }
            }
        }
        jack_free(ports);
    }

    jack_client_close(client);
    return found;
}

/* ─── JACK port enumeration ──────────────────────────────────────────────────
 *
 * Opens a temporary JACK client (JackNoStartServer so we don't accidentally
 * launch jackd), counts system:capture_N and system:playback_N audio ports,
 * then immediately closes the client.
 */
int hw_detect_jack_ports(hw_jack_ports_t *out)
{
    memset(out, 0, sizeof(*out));

    jack_status_t status;
    jack_client_t *client = jack_client_open("mpt_detect",
                                              JackNoStartServer, &status);
    if (!client) return -1;

    const char **ports;

    /* Audio capture: system outputs audio into JACK → JackPortIsOutput */
    ports = jack_get_ports(client, "^system:capture_",
                           JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
    if (ports) {
        while (ports[out->audio_capture]) out->audio_capture++;
        jack_free(ports);
    }

    /* Audio playback: JACK pushes audio into hardware → JackPortIsInput */
    ports = jack_get_ports(client, "^system:playback_",
                           JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    if (ports) {
        while (ports[out->audio_playback]) out->audio_playback++;
        jack_free(ports);
    }

    jack_client_close(client);
    return 0;
}
