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
 *          Uses `jack_lsp` (no JACK client API) to avoid realtime-thread
 *          teardown crashes.
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

    /* ── Phase 2: JACK software MIDI sources via jack_lsp ──
     *
     * jack_lsp --aliases lists every port followed by its aliases (indented).
     * We want output ports (sources) that are not system:*, not mod-host,
     * not mod-monitor, not mpt_*, and not the Midi-Through loopback. */
    FILE *lsp = popen(JACK_LSP " --aliases 2>/dev/null", "r");
    if (lsp) {
        char cur_port[128] = "";
        char line[512];
        bool cur_is_loopback = false;

        while (fgets(line, sizeof(line), lsp) && count < max) {
            line[strcspn(line, "\r\n")] = '\0';

            if (line[0] != ' ' && line[0] != '\t') {
                /* Port name line — emit previous port if it was a valid software source */
                if (cur_port[0] && !cur_is_loopback) {
                    /* Skip system:*, mod-host, mod-monitor, mpt_* */
                    bool skip =
                        strncmp(cur_port, "system:",      7) == 0 ||
                        strncmp(cur_port, "mod-host",     8) == 0 ||
                        strncmp(cur_port, "mod-monitor", 11) == 0 ||
                        strncmp(cur_port, "mpt_",         4) == 0;
                    if (!skip) {
                        /* jack_lsp lists all ports; we want sources (output ports).
                         * Without -p we can't tell direction easily — we keep any
                         * non-system non-monitor client port as a potential source.
                         * The caller uses is_input=true / is_output=false to indicate
                         * "this is a MIDI source mod-host can read from". */
                        char label[64];
                        jack_midi_port_label(cur_port, label, sizeof(label));
                        snprintf(out[count].dev,   sizeof(out[count].dev),   "%s", cur_port);
                        snprintf(out[count].label, sizeof(out[count].label), "%s", label);
                        out[count].is_input  = true;
                        out[count].is_output = false;
                        count++;
                    }
                }
                snprintf(cur_port, sizeof(cur_port), "%s", line);
                cur_is_loopback = false;
            } else {
                /* Alias line — mark loopback if Midi-Through alias found */
                const char *a = line + strspn(line, " \t");
                if (strcasestr(a, "Midi-Through") || strcasestr(a, "midi_through"))
                    cur_is_loopback = true;
            }
        }

        /* Emit last port */
        if (cur_port[0] && !cur_is_loopback && count < max) {
            bool skip =
                strncmp(cur_port, "system:",      7) == 0 ||
                strncmp(cur_port, "mod-host",     8) == 0 ||
                strncmp(cur_port, "mod-monitor", 11) == 0 ||
                strncmp(cur_port, "mpt_",         4) == 0;
            if (!skip) {
                char label[64];
                jack_midi_port_label(cur_port, label, sizeof(label));
                snprintf(out[count].dev,   sizeof(out[count].dev),   "%s", cur_port);
                snprintf(out[count].label, sizeof(out[count].label), "%s", label);
                out[count].is_input  = true;
                out[count].is_output = false;
                count++;
            }
        }

        pclose(lsp);
    }

    return count;
}

/* ─── MIDI loopback detection ───────────────────────────────────────────────
 *
 * Uses jack_lsp --aliases to find the system:midi_capture_N port whose alias
 * contains "Midi-Through".  Returns that port name (the JACK capture port that
 * reads from Midi-Through; the caller derives the playback port from it).
 * No JACK client API is used, avoiding realtime-thread teardown crashes.
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
            /* Port name — only track system:midi_capture_* */
            if (strncmp(line, "system:midi_capture_", 20) == 0)
                snprintf(cur, sizeof(cur), "%s", line);
            else
                cur[0] = '\0';
        } else if (cur[0]) {
            /* Alias line under a midi_capture port — check for Midi-Through */
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
