#include "hw_detect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/* ─── MIDI (ALSA raw-MIDI ports via amidi) ───────────────────────────────────
 *
 * `amidi -l` output:
 *   Dir Device    Name
 *   IO  hw:1,0,0  pisound MIDI
 *    I  hw:2,0,0  USB MIDI Interface MIDI 1
 */
int hw_detect_midi(hw_midi_port_t *out, int max)
{
    FILE *f = popen("amidi -l 2>/dev/null", "r");
    if (!f) return 0;

    int  count = 0;
    char line[256];

    /* Skip header line */
    if (!fgets(line, sizeof(line), f)) { pclose(f); return 0; }

    while (fgets(line, sizeof(line), f) && count < max) {
        char mode[4] = "", dev[64] = "", label[128] = "";

        /* Direction field may be preceded by spaces */
        if (sscanf(line, " %3s %63s %127[^\n]", mode, dev, label) < 2)
            continue;
        if (dev[0] == '\0') continue;

        /* Trim trailing spaces from label */
        if (label[0]) {
            char *end = label + strlen(label) - 1;
            while (end >= label && (*end == ' ' || *end == '\r')) *end-- = '\0';
        }

        snprintf(out[count].dev,   sizeof(out[count].dev),   "%s", dev);
        snprintf(out[count].label, sizeof(out[count].label), "%s",
                 label[0] ? label : dev);
        out[count].is_input  = (strchr(mode, 'I') != NULL);
        out[count].is_output = (strchr(mode, 'O') != NULL);
        count++;
    }

    pclose(f);
    return count;
}
