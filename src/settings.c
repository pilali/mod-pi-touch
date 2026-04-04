#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    /* mod-host */
    env_str("MPT_HOST_ADDR", s->host_addr, sizeof(s->host_addr), MPT_DEFAULT_HOST_ADDR);
    s->host_cmd_port = env_int("MPT_HOST_CMD_PORT", MPT_DEFAULT_HOST_CMD_PORT);
    s->host_fb_port  = env_int("MPT_HOST_FB_PORT",  MPT_DEFAULT_HOST_FB_PORT);

    /* Display/input */
    env_str("MPT_FB_DEVICE",    s->fb_device,    sizeof(s->fb_device),    MPT_DEFAULT_FB_DEVICE);
    env_str("MPT_TOUCH_DEVICE", s->touch_device, sizeof(s->touch_device), MPT_DEFAULT_TOUCH_DEVICE);

    /* Defaults */
    s->dark_mode       = true;
    s->ui_scale_percent = 100;

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
                cJSON_Delete(root);
            }
            free(buf);
        }
        fclose(f);
    }

    g_settings   = *s;
    g_initialized = true;
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
    cJSON_AddBoolToObject(root,   "dark_mode", s->dark_mode);
    cJSON_AddNumberToObject(root, "ui_scale",  s->ui_scale_percent);
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
