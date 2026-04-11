#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include <lvgl.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>
#include <src/drivers/evdev/lv_evdev.h>

#include "settings.h"
#include "i18n.h"
#include "host_comm.h"
#include "plugin_manager.h"
#include "lv2_utils.h"
#include "ui/ui_app.h"
#include "ui/ui_pedalboard.h"
#include "cJSON.h"

/* ─── Signal handling ────────────────────────────────────────────────────────── */
static volatile bool g_running = true;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = false;
}

/* ─── Feedback handler (mod-host → UI) ──────────────────────────────────────── */
static void feedback_handler(const char *msg, void *ud)
{
    (void)ud;
    /* Parse monitoring messages: "param <instance> <symbol> <value>" */
    int  instance;
    char symbol[128];
    float value;
    if (sscanf(msg, "param %d %127s %f", &instance, symbol, &value) == 3) {
        /* Update UI (must be called from LVGL thread — use a mutex or LVGL event) */
        /* For now just update the internal state; visual update happens on next refresh */
        ui_pedalboard_update_param(instance, symbol, value);
    }
}

/* ─── LVGL tick thread ───────────────────────────────────────────────────────── */
static void *tick_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        lv_tick_inc(5);
        usleep(5000); /* 5 ms */
    }
    return NULL;
}

/* ─── Main ───────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* ── Settings ── */
    mpt_settings_t settings;
    settings_init(&settings);
    i18n_set_lang(i18n_lang_from_code(settings.language));

    /* ── LVGL init ── */
    lv_init();

    /* Framebuffer display */
    lv_display_t *disp = lv_linux_fbdev_create();
    if (!disp) {
        fprintf(stderr, "[main] Failed to create framebuffer display on %s\n",
                settings.fb_device);
        return 1;
    }
    lv_linux_fbdev_set_file(disp, settings.fb_device);

    /* Rotate 270° in software — physical framebuffer is 720×1280 portrait,
     * this gives LVGL a logical 1280×720 landscape coordinate space.
     * The touch reports in physical portrait (720×1280) without swapxy in
     * config.txt; LVGL's indev rotation correctly maps it to landscape. */
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

    /* Evdev touch input */
    lv_indev_t *indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, settings.touch_device);
    if (!indev) {
        fprintf(stderr, "[main] Warning: cannot open touch device %s\n",
                settings.touch_device);
        /* Non-fatal: can run without touch for debugging */
    } else {
        lv_indev_set_display(indev, disp);
        lv_indev_set_long_press_time(indev, 800); /* ms — long press opens param editor */
    }

    /* ── LV2 world ── */
    lv2u_world_init();

    /* ── Plugin discovery ── */
    printf("[main] Scanning LV2 plugins...\n");
    {
        char lv2_path[1024];
        snprintf(lv2_path, sizeof(lv2_path), "%s:%s",
                 settings.lv2_user_dir, settings.lv2_system_dir);
        pm_init(lv2_path, settings.plugin_cache_file);
    }
    printf("[main] Found %d plugins\n", pm_plugin_count());

    /* ── mod-host connection ── */
    printf("[main] Connecting to mod-host at %s:%d...\n",
           settings.host_addr, settings.host_cmd_port);
    if (host_comm_connect(settings.host_addr,
                          settings.host_cmd_port,
                          settings.host_fb_port,
                          feedback_handler, NULL) < 0) {
        fprintf(stderr, "[main] Warning: cannot connect to mod-host. "
                "Start mod-host first.\n");
        /* Continue anyway — user can see disconnected status in settings */
    }

    /* ── Build UI ── */
    ui_app_init();

    /* ── Auto-load last pedalboard + snapshot ── */
    {
        FILE *f = fopen(settings.last_state_file, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            rewind(f);
            char *buf = malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, f);
                buf[sz] = '\0';
                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    cJSON *jpath = cJSON_GetObjectItem(root, "pedalboard");
                    cJSON *jsnap = cJSON_GetObjectItem(root, "snapshot");
                    if (cJSON_IsString(jpath) && jpath->valuestring[0]) {
                        printf("[main] Auto-loading last pedalboard: %s\n",
                               jpath->valuestring);
                        ui_pedalboard_load(jpath->valuestring);
                        if (cJSON_IsNumber(jsnap)) {
                            int snap_idx = (int)jsnap->valuedouble;
                            pedalboard_t *pb = ui_pedalboard_get();
                            if (pb && snap_idx >= 0 && snap_idx < pb->snapshot_count
                                    && snap_idx != pb->current_snapshot) {
                                printf("[main] Restoring snapshot %d\n", snap_idx);
                                ui_pedalboard_apply_snapshot(snap_idx);
                            }
                        }
                    }
                    cJSON_Delete(root);
                }
                free(buf);
            }
            fclose(f);
        }
    }

    /* ── LVGL tick thread ── */
    pthread_t tick_tid;
    pthread_create(&tick_tid, NULL, tick_thread, NULL);

    /* ── Main loop ── */
    printf("[main] mod-pi-touch running. Press Ctrl+C to exit.\n");
    while (g_running) {
        uint32_t time_till_next = lv_timer_handler();
        if (time_till_next > 10) time_till_next = 10;
        usleep(time_till_next * 1000);
    }

    /* ── Cleanup ── */
    printf("[main] Shutting down...\n");
    pthread_join(tick_tid, NULL);

    host_comm_disconnect();
    pm_fini();
    lv2u_world_fini();

    return 0;
}
