#include "ui_file_browser.h"
#include "ui_app.h"
#include "../i18n.h"

#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define PATH_MAX_LEN 1024
#define MAX_ENTRIES  256

typedef struct {
    char name[256];
    bool is_dir;
} dir_entry_t;

/* ─── State ─────────────────────────────────────────────────────────────────── */
static lv_obj_t         *g_modal     = NULL;
static lv_obj_t         *g_list      = NULL;
static lv_obj_t         *g_path_lbl  = NULL;
static char              g_cur_dir[PATH_MAX_LEN];
static file_browser_cb_t g_cb        = NULL;
static void             *g_ud        = NULL;

/* Extension filter — NULL means show all */
#define MAX_EXTS 16
static char  g_exts[MAX_EXTS][16];
static int   g_ext_count = 0;

/* Returns true if filename passes the extension filter */
static bool ext_matches(const char *name)
{
    if (g_ext_count == 0) return true;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    /* Compare lowercase */
    char lower[16] = {0};
    for (int i = 0; dot[i] && i < 15; i++)
        lower[i] = (char)tolower((unsigned char)dot[i]);
    for (int i = 0; i < g_ext_count; i++)
        if (strcmp(lower, g_exts[i]) == 0) return true;
    return false;
}

/* ─── Entry comparison (dirs first, then alphabetical) ──────────────────────── */
static int entry_cmp(const void *a, const void *b)
{
    const dir_entry_t *ea = a, *eb = b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

/* ─── Forward declarations ───────────────────────────────────────────────────── */
static void populate_list(void);

/* ─── List item click ────────────────────────────────────────────────────────── */
typedef struct {
    char name[256];
    bool is_dir;
} entry_ud_t;

static entry_ud_t g_entry_pool[MAX_ENTRIES];
static int        g_entry_pool_n = 0;

static void on_entry_click(lv_event_t *e)
{
    entry_ud_t *eu = lv_event_get_user_data(e);
    if (!eu) return;

    if (eu->is_dir) {
        /* Navigate into directory */
        char next[PATH_MAX_LEN];
        if (strcmp(eu->name, "..") == 0) {
            /* Go up */
            char *sl = strrchr(g_cur_dir, '/');
            if (sl && sl != g_cur_dir) {
                *sl = '\0';
            } else {
                g_cur_dir[0] = '/';
                g_cur_dir[1] = '\0';
            }
        } else {
            size_t cur_len = strlen(g_cur_dir);
            if (cur_len > 1) /* not root "/" */
                snprintf(next, sizeof(next), "%s/%s", g_cur_dir, eu->name);
            else
                snprintf(next, sizeof(next), "/%s", eu->name);
            snprintf(g_cur_dir, sizeof(g_cur_dir), "%s", next);
        }
        populate_list();
    } else {
        /* File selected */
        char full_path[PATH_MAX_LEN * 2];
        if (strcmp(g_cur_dir, "/") == 0)
            snprintf(full_path, sizeof(full_path), "/%s", eu->name);
        else
            snprintf(full_path, sizeof(full_path), "%s/%s", g_cur_dir, eu->name);

        file_browser_cb_t cb = g_cb;
        void *ud = g_ud;
        ui_file_browser_close();
        if (cb) cb(full_path, ud);
    }
}

/* ─── Cancel button ──────────────────────────────────────────────────────────── */
static void on_cancel(lv_event_t *e)
{
    (void)e;
    file_browser_cb_t cb = g_cb;
    void *ud = g_ud;
    ui_file_browser_close();
    if (cb) cb(NULL, ud);
}

/* ─── Populate list from g_cur_dir ──────────────────────────────────────────── */
static void populate_list(void)
{
    if (!g_list) return;

    lv_obj_clean(g_list);
    g_entry_pool_n = 0;

    /* Update path label */
    if (g_path_lbl)
        lv_label_set_text(g_path_lbl, g_cur_dir);

    /* Collect entries */
    static dir_entry_t entries[MAX_ENTRIES];
    int count = 0;

    DIR *d = opendir(g_cur_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && count < MAX_ENTRIES) {
            if (de->d_name[0] == '.') continue; /* skip hidden */

            char full[PATH_MAX_LEN * 2];
            snprintf(full, sizeof(full), "%s/%s", g_cur_dir, de->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;

            bool is_dir = S_ISDIR(st.st_mode);
            /* Apply extension filter to files (always show dirs) */
            if (!is_dir && !ext_matches(de->d_name)) continue;

            snprintf(entries[count].name, sizeof(entries[count].name),
                     "%s", de->d_name);
            entries[count].is_dir = is_dir;
            count++;
        }
        closedir(d);
    }

    qsort(entries, count, sizeof(dir_entry_t), entry_cmp);

    /* Add ".." for going up (unless at root) */
    if (strcmp(g_cur_dir, "/") != 0) {
        entry_ud_t *eu = &g_entry_pool[g_entry_pool_n++];
        snprintf(eu->name, sizeof(eu->name), "..");
        eu->is_dir = true;

        lv_obj_t *btn = lv_list_add_button(g_list, LV_SYMBOL_UP, "[..]");
        lv_obj_set_style_text_color(btn, lv_color_hex(0xAAAACC), 0);
        lv_obj_add_event_cb(btn, on_entry_click, LV_EVENT_CLICKED, eu);
    }

    /* Add entries */
    for (int i = 0; i < count && g_entry_pool_n < MAX_ENTRIES; i++) {
        entry_ud_t *eu = &g_entry_pool[g_entry_pool_n++];
        snprintf(eu->name, sizeof(eu->name), "%s", entries[i].name);
        eu->is_dir = entries[i].is_dir;

        const char *icon = entries[i].is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
        lv_obj_t *btn = lv_list_add_button(g_list, icon, entries[i].name);
        if (entries[i].is_dir)
            lv_obj_set_style_text_color(btn, lv_color_hex(0xCCCCFF), 0);
        lv_obj_add_event_cb(btn, on_entry_click, LV_EVENT_CLICKED, eu);
    }

    if (count == 0 && strcmp(g_cur_dir, "/") == 0) {
        lv_list_add_text(g_list, TR(TR_FILE_EMPTY));
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_file_browser_open(const char *start_dir,
                          const char *title,
                          const char **exts,
                          file_browser_cb_t cb,
                          void *userdata)
{
    if (g_modal) ui_file_browser_close();

    g_cb = cb;
    g_ud = userdata;
    snprintf(g_cur_dir, sizeof(g_cur_dir), "%s",
             (start_dir && start_dir[0]) ? start_dir : "/");

    /* Store extension filter */
    g_ext_count = 0;
    if (exts) {
        for (int i = 0; exts[i] && g_ext_count < MAX_EXTS; i++) {
            /* Store as lowercase */
            int j = 0;
            for (; exts[i][j] && j < 15; j++)
                g_exts[g_ext_count][j] = (char)tolower((unsigned char)exts[i][j]);
            g_exts[g_ext_count][j] = '\0';
            g_ext_count++;
        }
    }

    /* Modal overlay */
    g_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_80, 0);

    /* Panel */
    lv_obj_t *panel = lv_obj_create(g_modal);
    lv_obj_set_size(panel, 800, 600);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 8, 0);

    /* Title row */
    lv_obj_t *title_row = lv_obj_create(panel);
    lv_obj_set_size(title_row, LV_PCT(100), 44);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);

    lv_obj_t *title_lbl = lv_label_create(title_row);
    lv_label_set_text(title_lbl, title ? title : TR(TR_FILE_SELECT_TITLE));
    lv_obj_set_style_text_color(title_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_18, 0);

    lv_obj_t *btn_cancel = lv_btn_create(title_row);
    lv_obj_set_size(btn_cancel, 36, 36);
    lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_x = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_x);
    lv_obj_add_event_cb(btn_cancel, on_cancel, LV_EVENT_CLICKED, NULL);

    /* Current path label */
    g_path_lbl = lv_label_create(panel);
    lv_label_set_text(g_path_lbl, g_cur_dir);
    lv_label_set_long_mode(g_path_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(g_path_lbl, LV_PCT(100));
    lv_obj_set_style_text_color(g_path_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(g_path_lbl, &lv_font_montserrat_12, 0);

    /* File list */
    g_list = lv_list_create(panel);
    lv_obj_set_size(g_list, LV_PCT(100), 0);
    lv_obj_set_flex_grow(g_list, 1);
    lv_obj_set_style_bg_color(g_list, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    lv_obj_set_style_text_font(g_list, &lv_font_montserrat_14, 0);

    populate_list();
}

void ui_file_browser_close(void)
{
    if (g_modal) {
        lv_obj_delete_async(g_modal);
        g_modal    = NULL;
        g_list     = NULL;
        g_path_lbl = NULL;
        g_cb       = NULL;
        g_ud       = NULL;
    }
}
