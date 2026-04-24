#include "settings.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>

#include "cJSON.h"

static mpt_settings_t g_settings;
static bool           g_initialized = false;

/* ─── Helpers ──────────────────────────────────────────────────────────────── */
static void env_str(const char *var, char *dst, size_t dstsz, const char *def)
{
    const char *v = getenv(var);
    snprintf(dst, dstsz, "%s", v ? v : def);
}

static int env_int(const char *var, int def)
{
    const char *v = getenv(var);
    return v ? atoi(v) : def;
}

static void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return;
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "[settings] Cannot create dir %s: %s\n", path, strerror(errno));
}

/* ─── Public API ────────────────────────────────────────────────────────────── */
void settings_init(mpt_settings_t *s)
{
    memset(s, 0, sizeof(*s));

    /* Resolve home directory dynamically.
     * When launched via sudo, $HOME is /root rather than the calling user's home.
     * Use SUDO_USER env var (set by sudo) to find the real home directory. */
    const char *home = NULL;
    const char *sudo_user = getenv("SUDO_USER");
    if (sudo_user && sudo_user[0] != '\0') {
        struct passwd *pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir) home = pw->pw_dir;
    }
    if (!home || home[0] == '\0') home = getenv("HOME");
    if (!home || home[0] == '\0') home = "/home/pi";

    char def_data[512], def_pb[512], def_lv2[512];
    snprintf(def_data, sizeof(def_data), "%s/.mod-pi-touch", home);
    snprintf(def_pb,   sizeof(def_pb),   "%s/.pedalboards",  home);
    snprintf(def_lv2,  sizeof(def_lv2),  "%s/.lv2",          home);

    /* Directories */
    env_str("MPT_DATA_DIR",    s->data_dir,               sizeof(s->data_dir),    def_data);
    env_str("MPT_PEDALBOARDS", s->pedalboards_dir,         sizeof(s->pedalboards_dir), def_pb);
    env_str("MPT_FACTORY_DIR", s->factory_pedalboards_dir, sizeof(s->factory_pedalboards_dir),
            MPT_DEFAULT_FACTORY_DIR);
    env_str("LV2_PATH",        s->lv2_user_dir,            sizeof(s->lv2_user_dir), def_lv2);
    snprintf(s->lv2_system_dir, sizeof(s->lv2_system_dir), "%s", MPT_DEFAULT_LV2_SYSTEM_DIR);

    /* Derived file paths */
    /* Banks are stored by mod-ui in ~/data/banks.json, not in our data dir. */
    char def_banks[512];
    snprintf(def_banks, sizeof(def_banks), "%s/data/banks.json", home);
    env_str("MPT_BANKS_FILE", s->banks_file, sizeof(s->banks_file), def_banks);
    snprintf(s->last_state_file,  sizeof(s->last_state_file),
             "%s/%s", s->data_dir, MPT_DEFAULT_LAST_STATE_FILE);
    snprintf(s->prefs_file,       sizeof(s->prefs_file),
             "%s/%s", s->data_dir, MPT_DEFAULT_PREFS_FILE);
    snprintf(s->plugin_cache_file, sizeof(s->plugin_cache_file),
             "%s/%s", s->data_dir, MPT_DEFAULT_PLUGIN_CACHE);

    /* User files (MOD user-files root) — override with MOD_USER_FILES_DIR */
    char def_user_files[512];
    snprintf(def_user_files, sizeof(def_user_files), "%s/data/user-files", home);
    env_str("MOD_USER_FILES_DIR", s->user_files_dir, sizeof(s->user_files_dir), def_user_files);

    /* mod-host */
    env_str("MPT_HOST_ADDR", s->host_addr, sizeof(s->host_addr), MPT_DEFAULT_HOST_ADDR);
    s->host_cmd_port = env_int("MPT_HOST_CMD_PORT", MPT_DEFAULT_HOST_CMD_PORT);
    s->host_fb_port  = env_int("MPT_HOST_FB_PORT",  MPT_DEFAULT_HOST_FB_PORT);

    /* Display/input */
    env_str("MPT_FB_DEVICE",    s->fb_device,    sizeof(s->fb_device),    MPT_DEFAULT_FB_DEVICE);
    env_str("MPT_TOUCH_DEVICE", s->touch_device, sizeof(s->touch_device), MPT_DEFAULT_TOUCH_DEVICE);

    /* Defaults */
    s->dark_mode        = true;
    s->ui_scale_percent = 100;
    snprintf(s->language, sizeof(s->language), "en");

    /* Audio/JACK defaults */
    snprintf(s->jack_audio_device, sizeof(s->jack_audio_device), "hw:0");
    s->jack_buffer_size  = 128;
    s->jack_bit_depth    = 24;
    s->audio_capture_ch  = 0;   /* 0 = not yet configured, infer from TTL */
    s->audio_playback_ch = 0;

    /* WiFi defaults */
    s->hotspot_enabled = false;
    snprintf(s->hotspot_password, sizeof(s->hotspot_password),
             "%s", WIFI_HOTSPOT_PASSWORD_DEF);

    /* pre-fx defaults */
    s->gate_enabled   = false;
    s->gate_threshold = -60.0f;
    s->gate_decay     = 10.0f;
    s->gate_mode      = 3;       /* Stereo */
    s->tuner_ref_freq = 440.0f;
    s->tuner_input    = 0;        /* both captures */

    /* Ensure directories exist */
    ensure_dir(s->data_dir);
    ensure_dir(s->pedalboards_dir);

    /* Load saved prefs if available */
    FILE *f = fopen(s->prefs_file, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        rewind(f);
        char *buf = malloc(len + 1);
        if (buf) {
            fread(buf, 1, len, f);
            buf[len] = '\0';
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *dm = cJSON_GetObjectItem(root, "dark_mode");
                if (cJSON_IsBool(dm)) s->dark_mode = cJSON_IsTrue(dm);
                cJSON *sc = cJSON_GetObjectItem(root, "ui_scale");
                if (cJSON_IsNumber(sc)) s->ui_scale_percent = (int)sc->valuedouble;
                cJSON *lang = cJSON_GetObjectItem(root, "language");
                if (cJSON_IsString(lang))
                    snprintf(s->language, sizeof(s->language), "%s", lang->valuestring);

                /* Audio / JACK */
                cJSON *jd = cJSON_GetObjectItem(root, "jack_device");
                if (cJSON_IsString(jd))
                    snprintf(s->jack_audio_device, sizeof(s->jack_audio_device),
                             "%s", jd->valuestring);
                cJSON *jb = cJSON_GetObjectItem(root, "jack_buffer");
                if (cJSON_IsNumber(jb)) s->jack_buffer_size = (int)jb->valuedouble;
                cJSON *jbit = cJSON_GetObjectItem(root, "jack_bits");
                if (cJSON_IsNumber(jbit)) s->jack_bit_depth = (int)jbit->valuedouble;
                cJSON *jich = cJSON_GetObjectItem(root, "audio_in_ch");
                if (cJSON_IsNumber(jich)) s->audio_capture_ch = (int)jich->valuedouble;
                cJSON *joch = cJSON_GetObjectItem(root, "audio_out_ch");
                if (cJSON_IsNumber(joch)) s->audio_playback_ch = (int)joch->valuedouble;

                /* MIDI ports */
                cJSON *jmidi = cJSON_GetObjectItem(root, "midi_ports");
                if (cJSON_IsArray(jmidi)) {
                    int mc = cJSON_GetArraySize(jmidi);
                    s->midi_port_count = 0;
                    for (int i = 0; i < mc && i < MPT_MAX_MIDI_PORTS; i++) {
                        cJSON *mp = cJSON_GetArrayItem(jmidi, i);
                        if (!cJSON_IsObject(mp)) continue;
                        mpt_midi_port_t *p = &s->midi_ports[s->midi_port_count++];
                        cJSON *mdev  = cJSON_GetObjectItem(mp, "dev");
                        cJSON *mdout = cJSON_GetObjectItem(mp, "dev_out");
                        cJSON *mlbl  = cJSON_GetObjectItem(mp, "label");
                        cJSON *min   = cJSON_GetObjectItem(mp, "input");
                        cJSON *mout  = cJSON_GetObjectItem(mp, "output");
                        cJSON *men   = cJSON_GetObjectItem(mp, "enabled");
                        if (cJSON_IsString(mdev))
                            snprintf(p->dev, sizeof(p->dev), "%s", mdev->valuestring);
                        if (cJSON_IsString(mdout))
                            snprintf(p->dev_out, sizeof(p->dev_out), "%s", mdout->valuestring);
                        if (cJSON_IsString(mlbl))
                            snprintf(p->label, sizeof(p->label), "%s", mlbl->valuestring);
                        p->is_input  = cJSON_IsTrue(min);
                        p->is_output = cJSON_IsTrue(mout);
                        p->enabled   = cJSON_IsTrue(men);
                    }
                }

                /* pre-fx */
                cJSON *ge = cJSON_GetObjectItem(root, "gate_enabled");
                if (cJSON_IsBool(ge))    s->gate_enabled   = cJSON_IsTrue(ge);
                cJSON *gt = cJSON_GetObjectItem(root, "gate_threshold");
                if (cJSON_IsNumber(gt))  s->gate_threshold = (float)gt->valuedouble;
                cJSON *gd = cJSON_GetObjectItem(root, "gate_decay");
                if (cJSON_IsNumber(gd))  s->gate_decay     = (float)gd->valuedouble;
                cJSON *gm = cJSON_GetObjectItem(root, "gate_mode");
                if (cJSON_IsNumber(gm))  s->gate_mode      = (int)gm->valuedouble;
                cJSON *tr = cJSON_GetObjectItem(root, "tuner_ref");
                if (cJSON_IsNumber(tr))  s->tuner_ref_freq = (float)tr->valuedouble;
                cJSON *ti = cJSON_GetObjectItem(root, "tuner_input");
                if (cJSON_IsNumber(ti))  s->tuner_input    = (int)ti->valuedouble;

                /* WiFi */
                cJSON *hs = cJSON_GetObjectItem(root, "hotspot_enabled");
                if (cJSON_IsBool(hs))    s->hotspot_enabled = cJSON_IsTrue(hs);
                cJSON *hp = cJSON_GetObjectItem(root, "hotspot_password");
                if (cJSON_IsString(hp) && hp->valuestring[0])
                    snprintf(s->hotspot_password, sizeof(s->hotspot_password),
                             "%s", hp->valuestring);

                cJSON_Delete(root);
            }
            free(buf);
        }
        fclose(f);
    }

    g_settings   = *s;
    g_initialized = true;
}

/* Only allow hw:X[,Y[,Z]] to prevent shell injection via jack_audio_device. */
static bool is_safe_alsa_device(const char *dev)
{
    if (!dev || strncmp(dev, "hw:", 3) != 0) return false;
    for (const char *p = dev + 3; *p; p++)
        if (!isdigit((unsigned char)*p) && *p != ',') return false;
    return dev[3] != '\0';  /* reject bare "hw:" */
}

int settings_apply_jack(const mpt_settings_t *s)
{
    if (!is_safe_alsa_device(s->jack_audio_device)) {
        fprintf(stderr, "[settings] Invalid JACK device: '%s'\n",
                s->jack_audio_device);
        return -1;
    }

    /* Write new /etc/jackdrc so the change survives reboots.
     * Use a temp file to avoid partial writes, then sudo-copy. */
    const char *shortflag = (s->jack_bit_depth == 16) ? " -S" : "";
    FILE *f = fopen("/tmp/jackdrc_new", "w");
    if (!f) {
        fprintf(stderr, "[settings] Cannot write /tmp/jackdrc_new\n");
        return -1;
    }
    fprintf(f,
            "#!/bin/sh\n"
            "exec env JACK_DRIVER_DIR=/usr/local/lib/jack "
            "/usr/local/bin/jackd -t 2000 -R -P 95 -d alsa -d %s "
            "-r 48000 -p %d -n 2 -X seq -s%s\n",
            s->jack_audio_device, s->jack_buffer_size, shortflag);
    fclose(f);

    /* Install jackdrc and restart service */
    int r = system("sudo cp /tmp/jackdrc_new /etc/jackdrc"
                   " && sudo systemctl restart jack");
    fprintf(stderr, "[settings] JACK restart (buf=%d dev=%s): %s\n",
            s->jack_buffer_size, s->jack_audio_device,
            r == 0 ? "ok" : "failed");
    return r;
}

mpt_settings_t *settings_get(void)
{
    if (!g_initialized)
        settings_init(&g_settings);
    return &g_settings;
}

int settings_save_prefs(const mpt_settings_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "dark_mode",    s->dark_mode);
    cJSON_AddNumberToObject(root, "ui_scale",     s->ui_scale_percent);
    cJSON_AddStringToObject(root, "language",     s->language);

    /* Audio / JACK */
    cJSON_AddStringToObject(root, "jack_device",  s->jack_audio_device);
    cJSON_AddNumberToObject(root, "jack_buffer",  s->jack_buffer_size);
    cJSON_AddNumberToObject(root, "jack_bits",    s->jack_bit_depth);
    cJSON_AddNumberToObject(root, "audio_in_ch",  s->audio_capture_ch);
    cJSON_AddNumberToObject(root, "audio_out_ch", s->audio_playback_ch);

    /* MIDI ports */
    cJSON *jmidi_arr = cJSON_AddArrayToObject(root, "midi_ports");
    for (int i = 0; i < s->midi_port_count; i++) {
        cJSON *mp = cJSON_CreateObject();
        cJSON_AddStringToObject(mp, "dev",     s->midi_ports[i].dev);
        cJSON_AddStringToObject(mp, "dev_out", s->midi_ports[i].dev_out);
        cJSON_AddStringToObject(mp, "label",   s->midi_ports[i].label);
        cJSON_AddBoolToObject(mp,   "input",   s->midi_ports[i].is_input);
        cJSON_AddBoolToObject(mp,   "output",  s->midi_ports[i].is_output);
        cJSON_AddBoolToObject(mp,   "enabled", s->midi_ports[i].enabled);
        cJSON_AddItemToArray(jmidi_arr, mp);
    }

    /* pre-fx */
    cJSON_AddBoolToObject(root,   "gate_enabled",   s->gate_enabled);
    cJSON_AddNumberToObject(root, "gate_threshold", s->gate_threshold);
    cJSON_AddNumberToObject(root, "gate_decay",     s->gate_decay);
    cJSON_AddNumberToObject(root, "gate_mode",      s->gate_mode);
    cJSON_AddNumberToObject(root, "tuner_ref",      s->tuner_ref_freq);
    cJSON_AddNumberToObject(root, "tuner_input",    s->tuner_input);

    /* WiFi */
    cJSON_AddBoolToObject(root,   "hotspot_enabled",  s->hotspot_enabled);
    cJSON_AddStringToObject(root, "hotspot_password", s->hotspot_password);

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!str) return -1;

    FILE *f = fopen(s->prefs_file, "w");
    if (!f) { free(str); return -1; }
    fputs(str, f);
    fclose(f);
    free(str);
    return 0;
}
