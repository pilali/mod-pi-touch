#include "ui_pedalboard.h"
#include "ui_app.h"
#include "ui_param_editor.h"
#include "ui_plugin_block.h"
#include "ui_snapshot_bar.h"
#include "../pedalboard.h"
#include "../snapshot.h"
#include "../host_comm.h"
#include "../settings.h"
#include "../hw_detect.h"
#include "../i18n.h"
#include "../pre_fx.h"

#include "../plugin_manager.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define show_toast(msg) ui_app_show_toast(msg)

/* ─── Global pedalboard state ────────────────────────────────────────────────── */
static pedalboard_t     g_pedalboard;
static bool             g_pb_loaded = false;
/* Protects g_pedalboard from concurrent access by the feedback thread
 * (which calls ui_pedalboard_update_param) and the main LVGL thread. */
static pthread_mutex_t  g_pb_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* ─── Canvas state ───────────────────────────────────────────────────────────── */
static lv_obj_t *g_canvas_scroll = NULL; /* scrollable container */
static lv_obj_t *g_canvas        = NULL; /* actual canvas for connection lines */

/* Canvas virtual size (larger than screen for scrolling) */
#define CANVAS_W 3000
#define CANVAS_H 2000

/* ─── Drag state ─────────────────────────────────────────────────────────────── */
typedef struct {
    lv_obj_t *block;
    int        instance_id;
    lv_coord_t ox, oy; /* offset within block at touch start */
} drag_t;
static drag_t g_drag = {0};

/* ─── I/O port indicators ────────────────────────────────────────────────────── */

#define IO_DOT       18   /* circle/square diameter */
#define IO_ROW_H     (IO_DOT + 32) /* vertical step between port indicators */
#define IO_COL_W     72   /* width reserved on each side for I/O column */
#define IO_LINE_X    26   /* x of the vertical bar within the I/O column */
#define IO_LINE_W     2   /* thickness of the vertical bar */

typedef struct {
    char label[16]; /* "In 1", "In 2", "MIDI", "Out 1"… */
    bool is_midi;
} io_port_desc_t;

/* Layout constants — also used by connection drawing code */
#define LAYOUT_BLOCK_W  160
#define LAYOUT_BLOCK_H  160
#define LAYOUT_H_GAP     80
#define LAYOUT_V_GAP     40
#define LAYOUT_STEP_X   (LAYOUT_BLOCK_W + LAYOUT_H_GAP)
#define LAYOUT_STEP_Y   (LAYOUT_BLOCK_H + LAYOUT_V_GAP)
#define LAYOUT_MAX_COLS  32
#define LAYOUT_MAX_ADJ   16

/* Forward declarations — these functions are defined later in the file */
static bool        uri_to_jack_port(const char *uri, const pedalboard_t *pb,
                                    char *out, size_t outsz);
static const char *uri_to_sysport(const char *uri, const pedalboard_t *pb);
static int         uri_to_plugin_idx(const char *uri, const pedalboard_t *pb);
/* ─── Connection management ──────────────────────────────────────────────────── */

#define CONN_GRPS_MAX  128
#define CONN_PORT_MAX   32
#define CONN_SQ_SIZE    22   /* connection square side in px — large enough for touch */
#define CONN_PANEL_H   300   /* connection panel height in px */
#define LINE_PTS_MAX   512   /* flat point pool: up to 4 pts per orthogonal line */

typedef enum {
    CONN_MODE_IDLE = 0,
    CONN_MODE_PANEL_OPEN,
    CONN_MODE_CONNECTING,
} conn_mode_t;

typedef struct { int src_idx; int dst_idx; } conn_group_t;

typedef struct {
    char symbol[128];
    char label[64];
    char jack_port[256];
    bool is_midi;
} conn_port_info_t;

/* Persistent state */
static conn_group_t   g_conn_groups[CONN_GRPS_MAX];
static int            g_conn_group_count = 0;
static conn_mode_t    g_conn_mode = CONN_MODE_IDLE;
static lv_obj_t      *g_conn_panel  = NULL;
static lv_obj_t      *g_choose_popup = NULL;
static int            g_conn_src_idx  = -99;
static int            g_conn_dst_idx  = -99;
static int            g_conn_sel_port = -1;
static conn_port_info_t g_src_ports[CONN_PORT_MAX];
static int              g_src_port_count = 0;
static lv_obj_t        *g_port_btns[CONN_PORT_MAX];

/* Layout cache — written in ui_pedalboard_refresh, read by callbacks */
static lv_coord_t     g_layout_x[PB_MAX_PLUGINS];
static lv_coord_t     g_layout_y[PB_MAX_PLUGINS];
static int            g_left_offset_c = 16;
static int            g_right_col_x_c = 0;
static int            g_n_in_c = 0;
static int            g_n_out_c = 0;
static io_port_desc_t g_io_in_c[16];
static io_port_desc_t g_io_out_c[16];
static lv_obj_t      *g_parent_obj = NULL;

/* Choose-input popup state */
static conn_port_info_t s_choose_ports[CONN_PORT_MAX];
static int              s_choose_count = 0;
static int              s_choose_dst_idx = -99;

/* lv_line needs persistent point arrays — flat pool, up to 4 pts per line */
static lv_point_precise_t s_pts[LINE_PTS_MAX];
static int                s_pts_used = 0;

/* ── Coordinate helpers ─────────────────────────────────────────────────────── */

static void elem_right_center(int idx, lv_coord_t *cx, lv_coord_t *cy)
{
    if (idx == -1) { /* system input column */
        *cx = (lv_coord_t)(IO_LINE_X + IO_LINE_W);
        *cy = (lv_coord_t)(UI_CANVAS_H / 2);
    } else if (idx >= 0 && idx < g_pedalboard.plugin_count) {
        *cx = g_layout_x[idx] + LAYOUT_BLOCK_W;
        *cy = g_layout_y[idx] + LAYOUT_BLOCK_H / 2;
    } else { *cx = 0; *cy = 0; }
}

static void elem_left_center(int idx, lv_coord_t *cx, lv_coord_t *cy)
{
    if (idx == -2) { /* system output column */
        *cx = (lv_coord_t)(g_right_col_x_c + IO_LINE_X);
        *cy = (lv_coord_t)(UI_CANVAS_H / 2);
    } else if (idx >= 0 && idx < g_pedalboard.plugin_count) {
        *cx = g_layout_x[idx];
        *cy = g_layout_y[idx] + LAYOUT_BLOCK_H / 2;
    } else { *cx = 0; *cy = 0; }
}

/* ── Port collection ────────────────────────────────────────────────────────── */

static int collect_src_ports(int src_idx, conn_port_info_t *out, int max)
{
    int count = 0;
    if (src_idx == -1) { /* system audio/MIDI inputs */
        int ai = 0, mi = 0;
        for (int i = 0; i < g_n_in_c && count < max; i++) {
            conn_port_info_t *p = &out[count];
            if (g_io_in_c[i].is_midi) {
                mi++;
                snprintf(p->symbol,    sizeof(p->symbol),    "midi_capture_%d", mi);
                snprintf(p->label,     sizeof(p->label),     "%s", g_io_in_c[i].label);
                snprintf(p->jack_port, sizeof(p->jack_port), "system:midi_capture_%d", mi);
                p->is_midi = true;
            } else {
                ai++;
                snprintf(p->symbol,    sizeof(p->symbol),    "capture_%d", ai);
                snprintf(p->label,     sizeof(p->label),     "%s", g_io_in_c[i].label);
                snprintf(p->jack_port, sizeof(p->jack_port), "system:capture_%d", ai);
                p->is_midi = false;
            }
            count++;
        }
    } else if (src_idx >= 0 && src_idx < g_pedalboard.plugin_count) {
        pb_plugin_t *plug = &g_pedalboard.plugins[src_idx];
        const pm_plugin_info_t *info = pm_plugin_by_uri(plug->uri);
        if (info) {
            for (int j = 0; j < info->port_count && count < max; j++) {
                pm_port_type_t t = info->ports[j].type;
                if (t != PM_PORT_AUDIO_OUT && t != PM_PORT_MIDI_OUT) continue;
                conn_port_info_t *p = &out[count];
                snprintf(p->symbol,    sizeof(p->symbol),    "%s", info->ports[j].symbol);
                snprintf(p->label,     sizeof(p->label),     "%s", info->ports[j].name);
                snprintf(p->jack_port, sizeof(p->jack_port), "effect_%d:%s",
                         plug->instance_id, info->ports[j].symbol);
                p->is_midi = (t == PM_PORT_MIDI_OUT);
                count++;
            }
        }
    }
    return count;
}

static int collect_dst_ports(int dst_idx, conn_port_info_t *out, int max, bool want_midi)
{
    int count = 0;
    if (dst_idx == -2) { /* system audio/MIDI outputs */
        int ai = 0, mi = 0;
        for (int i = 0; i < g_n_out_c && count < max; i++) {
            bool is_m = g_io_out_c[i].is_midi;
            if (is_m) mi++; else ai++;
            if (is_m != want_midi) continue;
            conn_port_info_t *p = &out[count];
            if (is_m) {
                snprintf(p->symbol,    sizeof(p->symbol),    "midi_playback_%d", mi);
                snprintf(p->label,     sizeof(p->label),     "%s", g_io_out_c[i].label);
                snprintf(p->jack_port, sizeof(p->jack_port), "system:midi_playback_%d", mi);
            } else {
                snprintf(p->symbol,    sizeof(p->symbol),    "playback_%d", ai);
                snprintf(p->label,     sizeof(p->label),     "%s", g_io_out_c[i].label);
                snprintf(p->jack_port, sizeof(p->jack_port), "system:playback_%d", ai);
            }
            p->is_midi = is_m;
            count++;
        }
    } else if (dst_idx >= 0 && dst_idx < g_pedalboard.plugin_count) {
        pb_plugin_t *plug = &g_pedalboard.plugins[dst_idx];
        const pm_plugin_info_t *info = pm_plugin_by_uri(plug->uri);
        if (info) {
            for (int j = 0; j < info->port_count && count < max; j++) {
                pm_port_type_t t = info->ports[j].type;
                if (t != PM_PORT_AUDIO_IN && t != PM_PORT_MIDI_IN) continue;
                bool is_m = (t == PM_PORT_MIDI_IN);
                if (is_m != want_midi) continue;
                conn_port_info_t *p = &out[count];
                snprintf(p->symbol,    sizeof(p->symbol),    "%s", info->ports[j].symbol);
                snprintf(p->label,     sizeof(p->label),     "%s", info->ports[j].name);
                snprintf(p->jack_port, sizeof(p->jack_port), "effect_%d:%s",
                         plug->instance_id, info->ports[j].symbol);
                p->is_midi = is_m;
                count++;
            }
        }
    }
    return count;
}

/* Build bundle-relative file:// URI for pb_add_connection */
static void make_bundle_uri(int elem_idx, const char *sym, char *out, size_t outsz)
{
    if (elem_idx == -1 || elem_idx == -2) {
        snprintf(out, outsz, "file://%s/%s", g_pedalboard.path, sym);
    } else if (elem_idx >= 0 && elem_idx < g_pedalboard.plugin_count) {
        snprintf(out, outsz, "file://%s/%s/%s",
                 g_pedalboard.path, g_pedalboard.plugins[elem_idx].symbol, sym);
    }
}

static const char *elem_name(int idx)
{
    if (idx == -1) return "System In";
    if (idx == -2) return "System Out";
    if (idx >= 0 && idx < g_pedalboard.plugin_count)
        return g_pedalboard.plugins[idx].label;
    return "?";
}

/* ── Panel close / hide ──────────────────────────────────────────────────────── */

/* Hide panel UI only — keeps conn state (src_idx, sel_port) for CONN_MODE_CONNECTING */
static void conn_panel_hide(void)
{
    if (g_conn_panel) {
        /* Hide immediately so the panel vanishes this frame, then delete
         * asynchronously so LVGL finishes processing the current event
         * before the object is freed (avoids stale input-device pointer). */
        lv_obj_add_flag(g_conn_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(g_conn_panel);
        g_conn_panel = NULL;
    }
}

/* Full close: hide panel + reset all state */
static void conn_panel_close(void)
{
    if (g_choose_popup) {
        lv_obj_add_flag(g_choose_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(g_choose_popup);
        g_choose_popup = NULL;
    }
    conn_panel_hide();
    g_conn_mode      = CONN_MODE_IDLE;
    g_conn_src_idx   = -99;
    g_conn_dst_idx   = -99;
    g_conn_sel_port  = -1;
    g_src_port_count = 0;
}

/* ── Choose-input popup ──────────────────────────────────────────────────────── */

static void choose_port_btn_cb(lv_event_t *e)
{
    int pi = (int)(intptr_t)lv_event_get_user_data(e);
    if (pi < 0 || pi >= s_choose_count || g_conn_sel_port < 0) return;

    const char *src_jack = g_src_ports[g_conn_sel_port].jack_port;
    const char *dst_jack = s_choose_ports[pi].jack_port;
    host_connect(src_jack, dst_jack);

    char from_uri[PB_URI_MAX], to_uri[PB_URI_MAX];
    make_bundle_uri(g_conn_src_idx, g_src_ports[g_conn_sel_port].symbol,
                    from_uri, sizeof(from_uri));
    make_bundle_uri(s_choose_dst_idx, s_choose_ports[pi].symbol,
                    to_uri, sizeof(to_uri));
    pb_add_connection(&g_pedalboard, from_uri, to_uri);
    g_pedalboard.modified = true;
    ui_app_update_title(g_pedalboard.name, true);

    char toast_msg[128];
    snprintf(toast_msg, sizeof(toast_msg), "%s  ->  %s",
             elem_name(g_conn_src_idx),
             (s_choose_dst_idx >= 0 && s_choose_dst_idx < g_pedalboard.plugin_count)
                 ? g_pedalboard.plugins[s_choose_dst_idx].label
                 : elem_name(s_choose_dst_idx));
    /* Close the chooser popup before full close (avoids double-free in conn_panel_close) */
    if (g_choose_popup) {
        lv_obj_add_flag(g_choose_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(g_choose_popup);
        g_choose_popup = NULL;
    }
    conn_panel_close();
    ui_pedalboard_refresh();
    show_toast(toast_msg);
}

static void show_input_chooser(int dst_idx, conn_port_info_t *ports, int n)
{
    s_choose_dst_idx = dst_idx;
    s_choose_count   = n;
    for (int i = 0; i < n; i++) s_choose_ports[i] = ports[i];

    int popup_h = 50 + n * 56;
    if (popup_h > 400) popup_h = 400;

    g_choose_popup = lv_obj_create(lv_layer_top());
    lv_obj_t *popup = g_choose_popup;
    lv_obj_set_size(popup, 420, popup_h);
    lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(popup, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(popup, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 8, 0);
    lv_obj_set_style_pad_all(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text(title, TR(TR_PB_CHOOSE_INPUT));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    for (int i = 0; i < n; i++) {
        lv_obj_t *btn = lv_btn_create(popup);
        lv_obj_set_size(btn, 390, 48);
        lv_obj_set_pos(btn, 0, 36 + i * 54);
        lv_obj_set_style_bg_color(btn, UI_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, ports[i].label);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, choose_port_btn_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);
    }
}


/* ── Execute connection to a clicked plugin ──────────────────────────────────── */

static void conn_target_selected(int dst_plugin_idx)
{
    if (g_conn_sel_port < 0 || g_conn_sel_port >= g_src_port_count) {
        conn_panel_close();
        return;
    }

    bool src_is_midi = g_src_ports[g_conn_sel_port].is_midi;
    conn_port_info_t dst_ports[CONN_PORT_MAX];
    int n_dst = collect_dst_ports(dst_plugin_idx, dst_ports, CONN_PORT_MAX, src_is_midi);

    if (n_dst == 0) {
        ui_app_show_toast_error(src_is_midi ? "No MIDI input on target." : "No audio input on target.");
        conn_panel_close();
        return;
    }

    if (n_dst == 1) {
        /* Single input — auto-connect */
        host_connect(g_src_ports[g_conn_sel_port].jack_port, dst_ports[0].jack_port);
        char from_uri[PB_URI_MAX], to_uri[PB_URI_MAX];
        make_bundle_uri(g_conn_src_idx, g_src_ports[g_conn_sel_port].symbol,
                        from_uri, sizeof(from_uri));
        make_bundle_uri(dst_plugin_idx, dst_ports[0].symbol,
                        to_uri, sizeof(to_uri));
        pb_add_connection(&g_pedalboard, from_uri, to_uri);
        g_pedalboard.modified = true;
        ui_app_update_title(g_pedalboard.name, true);

        char toast_msg[128];
        snprintf(toast_msg, sizeof(toast_msg), "%s  ->  %s",
                 elem_name(g_conn_src_idx), elem_name(dst_plugin_idx));
        conn_panel_close();
        ui_pedalboard_refresh();
        show_toast(toast_msg);
        return;
    }

    /* Multiple inputs — show chooser */
    show_input_chooser(dst_plugin_idx, dst_ports, n_dst);
}

/* ── Panel callbacks ──────────────────────────────────────────────────────────── */

static void conn_port_btn_cb(lv_event_t *e)
{
    int pi = (int)(intptr_t)lv_event_get_user_data(e);
    g_conn_sel_port = pi;
    for (int i = 0; i < g_src_port_count; i++) {
        if (!g_port_btns[i]) continue;
        lv_color_t bg = (i == pi) ? UI_COLOR_PRIMARY : UI_COLOR_SURFACE;
        lv_obj_set_style_bg_color(g_port_btns[i], bg, 0);
    }
}

static void conn_btn_connect_cb(lv_event_t *e)
{
    (void)e;
    if (g_conn_sel_port < 0) {
        ui_app_show_toast(TR(TR_PB_TAP_SOURCE));
        return;
    }
    g_conn_mode = CONN_MODE_CONNECTING;
    conn_panel_hide(); /* close panel so canvas is fully accessible */
    show_toast("Tap a plugin to connect...");
}

static void conn_btn_disconnect_cb(lv_event_t *e)
{
    (void)e;
    if (g_conn_sel_port < 0 || g_conn_sel_port >= g_src_port_count) {
        ui_app_show_toast(TR(TR_PB_TAP_SOURCE));
        return;
    }

    const char *src_jack = g_src_ports[g_conn_sel_port].jack_port;
    int removed = 0;

    /* Remove ALL connections originating from the selected source port */
    for (int i = 0; i < g_pedalboard.connection_count; ) {
        pb_connection_t *conn = &g_pedalboard.connections[i];
        char from_jack[256], to_jack[256];
        bool fok = uri_to_jack_port(conn->from, &g_pedalboard, from_jack, sizeof(from_jack));
        bool tok = uri_to_jack_port(conn->to,   &g_pedalboard, to_jack,   sizeof(to_jack));

        if (fok && tok && strcmp(from_jack, src_jack) == 0) {
            host_disconnect(from_jack, to_jack);
            pb_remove_connection(&g_pedalboard, conn->from, conn->to);
            g_pedalboard.modified = true;
            removed++;
            continue; /* entry removed — don't advance i */
        }
        i++;
    }

    if (removed > 0)
        ui_app_update_title(g_pedalboard.name, true);

    conn_panel_close();
    ui_pedalboard_refresh();
}

static void conn_panel_close_btn_cb(lv_event_t *e) { (void)e; conn_panel_close(); }

/* ── Connection panel open ───────────────────────────────────────────────────── */

static void conn_panel_open(int src_idx)
{
    conn_panel_close();

    g_conn_src_idx   = src_idx;
    g_conn_dst_idx   = -99; /* unused: disconnect removes all from selected port */
    g_conn_mode      = CONN_MODE_PANEL_OPEN;
    g_src_port_count = collect_src_ports(src_idx, g_src_ports, CONN_PORT_MAX);
    memset(g_port_btns, 0, sizeof(g_port_btns));

    lv_obj_t *top = lv_layer_top();
    int scr_h = lv_obj_get_height(top);
    int scr_w = lv_obj_get_width(top);
    int panel_y = scr_h - CONN_PANEL_H;

    g_conn_panel = lv_obj_create(top);
    lv_obj_set_size(g_conn_panel, scr_w, CONN_PANEL_H);
    lv_obj_set_pos(g_conn_panel, 0, panel_y);
    lv_obj_set_style_bg_color(g_conn_panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(g_conn_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_conn_panel, 2, 0);
    lv_obj_set_style_border_side(g_conn_panel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(g_conn_panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_pad_all(g_conn_panel, 0, 0);
    lv_obj_set_style_radius(g_conn_panel, 0, 0);
    lv_obj_set_style_shadow_width(g_conn_panel, 0, 0);
    lv_obj_clear_flag(g_conn_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header bar ── */
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "Connections: %s", elem_name(src_idx));

    lv_obj_t *hdr_bar = lv_obj_create(g_conn_panel);
    lv_obj_set_size(hdr_bar, scr_w, 44);
    lv_obj_set_pos(hdr_bar, 0, 0);
    lv_obj_set_style_bg_color(hdr_bar, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(hdr_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr_bar, 0, 0);
    lv_obj_set_style_pad_all(hdr_bar, 0, 0);
    lv_obj_set_style_radius(hdr_bar, 0, 0);
    lv_obj_clear_flag(hdr_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *hdr_lbl = lv_label_create(hdr_bar);
    lv_label_set_text(hdr_lbl, hdr);
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t *x_btn = lv_btn_create(hdr_bar);
    lv_obj_set_size(x_btn, 44, 38);
    lv_obj_align(x_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(x_btn, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(x_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(x_btn, 0, 0);
    lv_obj_set_style_shadow_width(x_btn, 0, 0);
    lv_obj_t *x_lbl = lv_label_create(x_btn);
    lv_label_set_text(x_lbl, "X");
    lv_obj_set_style_text_color(x_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(x_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(x_lbl);
    lv_obj_add_event_cb(x_btn, conn_panel_close_btn_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* ── Left pane: source port buttons ── */
    int half_w = scr_w / 2;
    int content_y = 48;
    int content_h = CONN_PANEL_H - content_y;

    lv_obj_t *left = lv_obj_create(g_conn_panel);
    lv_obj_set_size(left, half_w - 8, content_h - 8);
    lv_obj_set_pos(left, 4, content_y + 4);
    lv_obj_set_style_bg_color(left, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 8, 0);
    lv_obj_set_style_radius(left, 4, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *src_title = lv_label_create(left);
    lv_label_set_text(src_title, TR(TR_PB_SOURCE_OUTPUT));
    lv_obj_set_style_text_color(src_title, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(src_title, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(src_title, 0, 0);

    int btn_w = (half_w - 36) / 2;
    if (btn_w > 170) btn_w = 170;
    int btn_h = 48, btn_gap = 6;
    int bx = 0, by = 20;

    for (int i = 0; i < g_src_port_count && i < CONN_PORT_MAX; i++) {
        lv_obj_t *btn = lv_btn_create(left);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, bx, by);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, g_src_ports[i].label);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, conn_port_btn_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);
        g_port_btns[i] = btn;

        if (i % 2 == 0) { bx = btn_w + btn_gap; }
        else             { bx = 0; by += btn_h + btn_gap; }
    }

    /* ── Right pane: action buttons ── */
    lv_obj_t *right = lv_obj_create(g_conn_panel);
    lv_obj_set_size(right, half_w - 8, content_h - 8);
    lv_obj_set_pos(right, half_w + 4, content_y + 4);
    lv_obj_set_style_bg_color(right, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 8, 0);
    lv_obj_set_style_radius(right, 4, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *conn_btn = lv_btn_create(right);
    lv_obj_set_size(conn_btn, half_w - 40, 64);
    lv_obj_align(conn_btn, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(conn_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(conn_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(conn_btn, 0, 0);
    lv_obj_set_style_radius(conn_btn, 6, 0);
    lv_obj_set_style_shadow_width(conn_btn, 0, 0);
    lv_obj_t *conn_lbl = lv_label_create(conn_btn);
    lv_label_set_text(conn_lbl, TR(TR_PB_CONNECT));
    lv_obj_set_style_text_color(conn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(conn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(conn_lbl);
    lv_obj_add_event_cb(conn_btn, conn_btn_connect_cb, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *disc_btn = lv_btn_create(right);
    lv_obj_set_size(disc_btn, half_w - 40, 64);
    lv_obj_align(disc_btn, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_bg_color(disc_btn, UI_COLOR_BYPASS, 0);
    lv_obj_set_style_bg_opa(disc_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc_btn, 0, 0);
    lv_obj_set_style_radius(disc_btn, 6, 0);
    lv_obj_set_style_shadow_width(disc_btn, 0, 0);
    lv_obj_t *disc_lbl = lv_label_create(disc_btn);
    lv_label_set_text(disc_lbl, TR(TR_PB_DISCONNECT));
    lv_obj_set_style_text_color(disc_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(disc_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(disc_lbl);
    lv_obj_add_event_cb(disc_btn, conn_btn_disconnect_cb, LV_EVENT_SHORT_CLICKED, NULL);
}

/* ── Square long-press ─────────────────────────────────────────────────────── */

static void on_conn_square_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    int src_idx = (int)(intptr_t)lv_event_get_user_data(e);
    conn_panel_open(src_idx);
}

/* ── Y coordinate for a specific system I/O port dot ─────────────────────────── */

static lv_coord_t sysport_y(const char *port_sym, bool is_input)
{
    io_port_desc_t *descs = is_input ? g_io_in_c : g_io_out_c;
    int n              = is_input ? g_n_in_c  : g_n_out_c;
    if (n == 0) return (lv_coord_t)(UI_CANVAS_H / 2);

    bool is_midi = (strncmp(port_sym, "midi", 4) == 0);
    const char *p = strrchr(port_sym, '_');
    int num = (p && *(p+1)) ? atoi(p + 1) : 1;   /* 1-based number in port name */

    int idx;
    if (!is_midi) {
        idx = num - 1;
    } else {
        int audio_cnt = 0;
        for (int i = 0; i < n; i++) if (!descs[i].is_midi) audio_cnt++;
        idx = audio_cnt + (num - 1);
    }
    if (idx < 0 || idx >= n) idx = 0;

    int total_h = n * IO_ROW_H - (IO_ROW_H - IO_DOT);
    int start_y = (UI_CANVAS_H - total_h) / 2;
    return (lv_coord_t)(start_y + idx * IO_ROW_H + IO_DOT / 2);
}

/* ── Draw connection lines ───────────────────────────────────────────────────── */

static void redraw_connections(void)
{
    if (!g_canvas_scroll) return;
    s_pts_used = 0;

    /* Gather unique (src, dst) pairs — used by draw_conn_squares for stub check */
    g_conn_group_count = 0;
    for (int c = 0; c < g_pedalboard.connection_count; c++) {
        const char *fu = g_pedalboard.connections[c].from;
        const char *tu = g_pedalboard.connections[c].to;
        const char *fsys = uri_to_sysport(fu, &g_pedalboard);
        const char *tsys = uri_to_sysport(tu, &g_pedalboard);
        int si = fsys ? -1 : uri_to_plugin_idx(fu, &g_pedalboard);
        int di = tsys ? -2 : uri_to_plugin_idx(tu, &g_pedalboard);
        if (!fsys && si < 0) continue;
        if (!tsys && di < 0) continue;
        bool dup = false;
        for (int k = 0; k < g_conn_group_count; k++)
            if (g_conn_groups[k].src_idx == si && g_conn_groups[k].dst_idx == di)
                { dup = true; break; }
        if (!dup && g_conn_group_count < CONN_GRPS_MAX) {
            g_conn_groups[g_conn_group_count].src_idx = si;
            g_conn_groups[g_conn_group_count].dst_idx = di;
            g_conn_group_count++;
        }
    }

    /* Draw one orthogonal line per individual connection with port-accurate y */
    for (int c = 0; c < g_pedalboard.connection_count; c++) {
        const char *fu = g_pedalboard.connections[c].from;
        const char *tu = g_pedalboard.connections[c].to;
        const char *fsys = uri_to_sysport(fu, &g_pedalboard);
        const char *tsys = uri_to_sysport(tu, &g_pedalboard);
        int si = fsys ? -1 : uri_to_plugin_idx(fu, &g_pedalboard);
        int di = tsys ? -2 : uri_to_plugin_idx(tu, &g_pedalboard);
        if (!fsys && si < 0) continue;
        if (!tsys && di < 0) continue;

        /* X positions from column edges */
        lv_coord_t x1, x2, tmp;
        elem_right_center(si, &x1, &tmp);
        elem_left_center (di, &x2, &tmp);

        /* Y: port-specific for system I/O, block centre for plugins */
        lv_coord_t y1 = (si == -1 && fsys)
            ? sysport_y(fsys, true)
            : (lv_coord_t)(g_layout_y[si] + LAYOUT_BLOCK_H / 2);

        lv_coord_t y2 = (di == -2 && tsys)
            ? sysport_y(tsys, false)
            : (lv_coord_t)(g_layout_y[di] + LAYOUT_BLOCK_H / 2);

        /* Vertical segment placed close to the destination (1/4 gap before it) */
        lv_coord_t mid_x = x2 - (lv_coord_t)(LAYOUT_H_GAP / 4);
        if (mid_x < x1 + 4) mid_x = x1 + 4;

        int n_pts;
        if (y1 == y2) {
            if (s_pts_used + 2 > LINE_PTS_MAX) break;
            s_pts[s_pts_used + 0] = (lv_point_precise_t){(float)x1, (float)y1};
            s_pts[s_pts_used + 1] = (lv_point_precise_t){(float)x2, (float)y1};
            n_pts = 2;
        } else {
            if (s_pts_used + 4 > LINE_PTS_MAX) break;
            s_pts[s_pts_used + 0] = (lv_point_precise_t){(float)x1,    (float)y1};
            s_pts[s_pts_used + 1] = (lv_point_precise_t){(float)mid_x, (float)y1};
            s_pts[s_pts_used + 2] = (lv_point_precise_t){(float)mid_x, (float)y2};
            s_pts[s_pts_used + 3] = (lv_point_precise_t){(float)x2,    (float)y2};
            n_pts = 4;
        }

        lv_obj_t *line = lv_line_create(g_canvas_scroll);
        lv_line_set_points(line, &s_pts[s_pts_used], n_pts);
        lv_obj_set_style_line_color(line, lv_color_hex(0x505050), 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_rounded(line, false, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_pts_used += n_pts;
    }

    /* Stub line for plugins with no outgoing connections */
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        bool has_out = false;
        for (int k = 0; k < g_conn_group_count; k++) {
            if (g_conn_groups[k].src_idx == i) { has_out = true; break; }
        }
        if (has_out) continue;
        if (s_pts_used + 2 > LINE_PTS_MAX) break;

        lv_coord_t x1, y1;
        elem_right_center(i, &x1, &y1);
        lv_coord_t stub_x = x1 + LAYOUT_H_GAP / 2;

        s_pts[s_pts_used + 0] = (lv_point_precise_t){(float)x1,     (float)y1};
        s_pts[s_pts_used + 1] = (lv_point_precise_t){(float)stub_x, (float)y1};

        lv_obj_t *line = lv_line_create(g_canvas_scroll);
        lv_line_set_points(line, &s_pts[s_pts_used], 2);
        lv_obj_set_style_line_color(line, lv_color_hex(0x404040), 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_rounded(line, false, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_pts_used += 2;
    }

}

/* ── IO column tap callbacks ─────────────────────────────────────────────────── */

static void on_io_col_tap(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    int src_idx = (int)(intptr_t)lv_event_get_user_data(e);
    conn_panel_open(src_idx);
}

static void on_io_out_tap(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    if (g_conn_mode == CONN_MODE_CONNECTING)
        conn_target_selected(-2);
}

/* ── Draw permanent connection squares (called AFTER plugin blocks) ─────────── */

static void draw_conn_squares(void)
{
    if (!g_canvas_scroll) return;

    /* One square per plugin, centered in the horizontal gap to the right */
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        lv_coord_t sx = g_layout_x[i] + LAYOUT_BLOCK_W + LAYOUT_H_GAP / 2 - CONN_SQ_SIZE / 2;
        lv_coord_t sy = g_layout_y[i] + LAYOUT_BLOCK_H / 2 - CONN_SQ_SIZE / 2;

        lv_obj_t *sq = lv_obj_create(g_canvas_scroll);
        lv_obj_set_size(sq, CONN_SQ_SIZE, CONN_SQ_SIZE);
        lv_obj_set_pos(sq, sx, sy);
        lv_obj_set_style_bg_color(sq, lv_color_hex(0x505878), 0);
        lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(sq, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_border_width(sq, 1, 0);
        lv_obj_set_style_radius(sq, 2, 0);
        lv_obj_set_style_shadow_width(sq, 0, 0);
        lv_obj_add_flag(sq, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(sq, on_conn_square_event, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

/* ─── Plugin click intercept (called from ui_plugin_block.c) ─────────────────── */

bool ui_pedalboard_intercept_plugin_click(int instance_id)
{
    if (g_conn_mode != CONN_MODE_CONNECTING) return false;
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        if (g_pedalboard.plugins[i].instance_id == instance_id) {
            conn_target_selected(i);
            return true;
        }
    }
    return false;
}

/* ─── Block event handlers ───────────────────────────────────────────────────── */

static void on_block_bypass(void *userdata);   /* forward declaration */
static void pb_load_ui_refresh(void *arg);     /* forward declaration */
static void pb_snapshot_ui_refresh(void *arg); /* forward declaration */
static void pb_apply_midi_mapped(int instance_id, const char *symbol,
                                 int ch, int cc, float min, float max); /* fwd */

static void on_patch_changed(int instance_id, const char *param_uri,
                             const char *path, void *userdata)
{
    (void)userdata;
    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (!plug) return;

    /* Update or add patch param in plugin */
    for (int i = 0; i < plug->patch_param_count; i++) {
        if (strcmp(plug->patch_params[i].uri, param_uri) == 0) {
            snprintf(plug->patch_params[i].path,
                     sizeof(plug->patch_params[i].path), "%s", path);
            g_pedalboard.modified = true;
            return;
        }
    }
    if (plug->patch_param_count < PB_MAX_PATCH_PARAMS) {
        pb_patch_t *pp = &plug->patch_params[plug->patch_param_count++];
        snprintf(pp->uri,  sizeof(pp->uri),  "%s", param_uri);
        snprintf(pp->path, sizeof(pp->path), "%s", path);
        g_pedalboard.modified = true;
    }
}

static void on_midi_mapped(int instance_id, const char *symbol,
                           int midi_ch, int midi_cc,
                           float min, float max, void *userdata)
{
    (void)userdata;
    /* Param editor already updated its own UI; just persist data model. */
    pb_apply_midi_mapped(instance_id, symbol, midi_ch, midi_cc, min, max);
}

static void on_block_tap(void *userdata)
{
    int instance_id = (int)(intptr_t)userdata;

    /* In connecting mode: already handled by intercept — nothing to do here */
    if (g_conn_mode == CONN_MODE_CONNECTING) return;

    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (!plug) return;

    ui_param_editor_show(instance_id, plug->label, plug->uri,
                         plug->ports, plug->port_count,
                         plug->patch_params, plug->patch_param_count,
                         plug->enabled,
                         on_block_bypass, (void *)(intptr_t)instance_id,
                         NULL, NULL,
                         on_patch_changed, NULL,
                         on_midi_mapped, NULL);
}

static void on_block_bypass(void *userdata)
{
    int instance_id = (int)(intptr_t)userdata;
    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (!plug) return;
    plug->enabled = !plug->enabled;
    host_bypass(instance_id, !plug->enabled);
    g_pedalboard.modified = true;
    ui_app_update_title(g_pedalboard.name, true);
    ui_pedalboard_refresh();
}

static void on_block_remove(void *userdata)
{
    int instance_id = (int)(intptr_t)userdata;
    host_remove_plugin(instance_id);
    pb_remove_plugin(&g_pedalboard, instance_id);
    g_pedalboard.modified = true;
    ui_app_update_title(g_pedalboard.name, true);
    ui_pedalboard_refresh();
}

/* ─── Automatic graph layout ─────────────────────────────────────────────────
 *
 * Ignores stored canvas_x/y coordinates (kept for mod-ui compatibility).
 * Computes positions from the connection graph:
 *   - Columns = topological depth (longest path from sources)
 *   - Rows    = position within column
 *   - Each parent is vertically centered between its successors
 *   - Each column is independently centered in the visible canvas height
 */

/* Extract system port name from URI. Returns pointer into static buffer or NULL. */
static const char *uri_to_sysport(const char *uri, const pedalboard_t *pb)
{
    char bundle_prefix[PB_PATH_MAX + 8];
    snprintf(bundle_prefix, sizeof(bundle_prefix), "file://%s/", pb->path);
    size_t prefix_len = strlen(bundle_prefix);

    const char *rel = NULL;
    if (strncmp(uri, bundle_prefix, prefix_len) == 0) {
        rel = uri + prefix_len;
    } else {
        const char *path = (strncmp(uri, "file://", 7) == 0) ? uri + 7 : uri;
        size_t blen = strlen(pb->path);
        if (strncmp(path, pb->path, blen) == 0 && path[blen] == '/')
            rel = path + blen + 1;
    }
    if (!rel) return NULL;
    if (strchr(rel, '/')) return NULL; /* plugin port, not system */
    return rel; /* "capture_1", "playback_2", "midi_capture_1" etc. */
}

/* Collect distinct system ports from 'from' (inputs) or 'to' (outputs) side.
 * Returns count; fills descs[]. */

/* Build the I/O descriptor list for one side of the pedalboard.
 *
 * Audio and MIDI are handled independently:
 *  - Audio: use audio_capture/playback_ch from settings if configured (>0),
 *    otherwise infer from TTL connections (legacy fallback).
 *  - MIDI: always use enabled ports from settings if any are known;
 *    otherwise infer from TTL connections.
 * This means the user can enable MIDI in settings without first configuring
 * the audio channel count, and vice-versa. */
static int collect_io_descs(bool want_inputs, io_port_desc_t *descs, int max)
{
    mpt_settings_t *s = settings_get();

    /* ── Audio: query JACK directly ── */
    hw_jack_ports_t jp;
    bool jack_ok = (hw_detect_jack_ports(&jp) == 0 &&
                    (jp.audio_capture > 0 || jp.audio_playback > 0));


    int count = 0;

    /* ── Audio: always ascending (1, 2, …) ── */
    int ach;
    if (jack_ok) {
        ach = want_inputs ? jp.audio_capture : jp.audio_playback;
    } else {
        /* JACK unreachable — default to 2 channels so ports are always visible */
        ach = 2;
    }
    for (int i = 0; i < ach && count < max; i++) {
        snprintf(descs[count].label, sizeof(descs[0].label),
                 want_inputs ? "In %d" : "Out %d", i + 1);
        descs[count].is_midi = false;
        count++;
    }

    /* ── MIDI from settings ── */
    int midi_idx = 0;
    for (int i = 0; i < s->midi_port_count && count < max; i++) {
        mpt_midi_port_t *p = &s->midi_ports[i];
        if (!p->enabled) continue;
        bool relevant = want_inputs ? p->is_input : p->is_output;
        if (!relevant) continue;
        midi_idx++;
        if (midi_idx == 1)
            snprintf(descs[count].label, sizeof(descs[0].label), "MIDI");
        else
            snprintf(descs[count].label, sizeof(descs[0].label), "M%d", midi_idx);
        descs[count].is_midi = true;
        count++;
    }

    return count;
}

/* Draw one I/O column (inputs on the left or outputs on the right).
 * x_col is the left edge of the column area in the scroll container. */
static void draw_io_column(lv_obj_t *parent, const io_port_desc_t *ports, int n,
                            int x_col, int canvas_h, bool is_right)
{
    if (n == 0) return;

    int total_h = n * IO_ROW_H - (IO_ROW_H - IO_DOT);
    int start_y = (canvas_h - total_h) / 2;

    /* Vertical bar */
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, IO_LINE_W, total_h + IO_DOT);
    lv_obj_set_pos(bar, x_col + IO_LINE_X, start_y);
    lv_obj_set_style_bg_color(bar, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < n; i++) {
        lv_coord_t cy = (lv_coord_t)(start_y + i * IO_ROW_H);

        /* Circle (audio) or square (MIDI) */
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_size(dot, IO_DOT, IO_DOT);
        lv_obj_set_pos(dot, x_col + IO_LINE_X - IO_DOT/2, cy);
        lv_obj_set_style_bg_color(dot,
            ports[i].is_midi ? UI_COLOR_ACCENT : UI_COLOR_ACTIVE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot,
            ports[i].is_midi ? 3 : LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        /* Label — to the right of the dot for inputs, left of the dot for outputs */
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, ports[i].label);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        if (is_right) {
            /* Right column: label to the LEFT of the dot, right-aligned */
            lv_obj_set_width(lbl, 44);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_pos(lbl, x_col + IO_LINE_X - IO_DOT/2 - 48, cy + 2);
        } else {
            /* Left column: label to the RIGHT of the dot */
            lv_obj_set_pos(lbl, x_col + IO_LINE_X + IO_DOT/2 + 4, cy + 2);
        }
    }
}

/* Return the index in pb->plugins[] for the plugin whose symbol matches the
 * URI, or -1 for system ports (capture/playback) and unknown URIs. */
static int uri_to_plugin_idx(const char *uri, const pedalboard_t *pb)
{
    char bundle_prefix[PB_PATH_MAX + 8];
    snprintf(bundle_prefix, sizeof(bundle_prefix), "file://%s/", pb->path);
    size_t prefix_len = strlen(bundle_prefix);

    const char *rel;
    if (strncmp(uri, bundle_prefix, prefix_len) == 0) {
        rel = uri + prefix_len;
    } else {
        const char *path = (strncmp(uri, "file://", 7) == 0) ? uri + 7 : uri;
        size_t blen = strlen(pb->path);
        if (strncmp(path, pb->path, blen) == 0 && path[blen] == '/')
            rel = path + blen + 1;
        else
            return -1;
    }

    const char *slash = strchr(rel, '/');
    if (!slash) return -1; /* hardware port */

    char sym[PB_SYMBOL_MAX];
    size_t slen = (size_t)(slash - rel);
    if (slen >= sizeof(sym)) return -1;
    memcpy(sym, rel, slen);
    sym[slen] = '\0';

    for (int i = 0; i < pb->plugin_count; i++)
        if (strcmp(pb->plugins[i].symbol, sym) == 0)
            return i;
    return -1;
}

/* Compute display positions for each plugin in pb->plugins[].
 * out_x / out_y arrays must have at least pb->plugin_count entries.
 * canvas_h is the visible height to center into. */
static void compute_layout(const pedalboard_t *pb,
                            lv_coord_t *out_x, lv_coord_t *out_y,
                            int canvas_h)
{
    int n = pb->plugin_count;
    if (n == 0) return;

    /* ── Build directed adjacency lists ── */
    int adj_out[PB_MAX_PLUGINS][LAYOUT_MAX_ADJ], deg_out[PB_MAX_PLUGINS];
    int adj_in [PB_MAX_PLUGINS][LAYOUT_MAX_ADJ], deg_in [PB_MAX_PLUGINS];
    memset(deg_out, 0, sizeof(deg_out));
    memset(deg_in,  0, sizeof(deg_in));

    for (int c = 0; c < pb->connection_count; c++) {
        int f = uri_to_plugin_idx(pb->connections[c].from, pb);
        int t = uri_to_plugin_idx(pb->connections[c].to,   pb);
        if (f < 0 || t < 0 || f == t) continue;
        /* Deduplicate */
        bool dup = false;
        for (int k = 0; k < deg_out[f]; k++) if (adj_out[f][k] == t) { dup = true; break; }
        if (!dup && deg_out[f] < LAYOUT_MAX_ADJ) adj_out[f][deg_out[f]++] = t;
        dup = false;
        for (int k = 0; k < deg_in[t]; k++)  if (adj_in[t][k]  == f) { dup = true; break; }
        if (!dup && deg_in[t]  < LAYOUT_MAX_ADJ) adj_in[t][deg_in[t]++]   = f;
    }

    /* ── Assign columns via Kahn's BFS (longest path from sources) ── */
    int col[PB_MAX_PLUGINS];
    int topo[PB_MAX_PLUGINS], topo_n = 0;
    int indeg_tmp[PB_MAX_PLUGINS];
    memset(col, 0, sizeof(col));
    for (int i = 0; i < n; i++) indeg_tmp[i] = deg_in[i];

    int q[PB_MAX_PLUGINS]; int qh = 0, qt = 0;
    for (int i = 0; i < n; i++) if (indeg_tmp[i] == 0) q[qt++] = i;

    while (qh < qt) {
        int u = q[qh++];
        topo[topo_n++] = u;
        for (int k = 0; k < deg_out[u]; k++) {
            int v = adj_out[u][k];
            if (col[u] + 1 > col[v]) col[v] = col[u] + 1;
            if (--indeg_tmp[v] == 0) q[qt++] = v;
        }
    }

    /* Remaining nodes (cycles / disconnected) placed in last column + 1 */
    int max_col = 0;
    for (int i = 0; i < n; i++) if (col[i] > max_col) max_col = col[i];
    for (int i = 0; i < n; i++) {
        bool found = false;
        for (int k = 0; k < topo_n; k++) if (topo[k] == i) { found = true; break; }
        if (!found) { col[i] = max_col + 1; topo[topo_n++] = i; }
    }
    max_col = 0;
    for (int i = 0; i < n; i++) if (col[i] > max_col) max_col = col[i];

    /* ── Group nodes by column in topological order → initial row assignment ── */
    int  col_nodes[LAYOUT_MAX_COLS][PB_MAX_PLUGINS];
    int  col_sz   [LAYOUT_MAX_COLS];
    memset(col_sz, 0, sizeof(col_sz));
    for (int ti = 0; ti < topo_n; ti++) {
        int u = topo[ti], c = col[u];
        if (c < LAYOUT_MAX_COLS) col_nodes[c][col_sz[c]++] = u;
    }

    /* row[] is floating-point: initially integer slots within each column */
    float row[PB_MAX_PLUGINS];
    for (int c = 0; c <= max_col; c++)
        for (int j = 0; j < col_sz[c]; j++)
            row[col_nodes[c][j]] = (float)j;

    /* ── Center each parent between its immediate successors (right to left) ── */
    for (int c = max_col - 1; c >= 0; c--) {
        for (int j = 0; j < col_sz[c]; j++) {
            int u = col_nodes[c][j];
            float sum = 0.0f; int cnt = 0;
            for (int k = 0; k < deg_out[u]; k++) {
                int v = adj_out[u][k];
                if (col[v] == c + 1) { sum += row[v]; cnt++; }
            }
            if (cnt > 0) row[u] = sum / (float)cnt;
        }
    }

    /* ── De-collide: within each column sort by row then enforce 1-unit gaps ── */
    for (int c = 0; c <= max_col; c++) {
        if (col_sz[c] <= 1) continue;

        /* Insertion sort col_nodes[c] by row value */
        for (int a = 1; a < col_sz[c]; a++) {
            int ua = col_nodes[c][a];
            float ra = row[ua];
            int b = a - 1;
            while (b >= 0 && row[col_nodes[c][b]] > ra) {
                col_nodes[c][b + 1] = col_nodes[c][b];
                b--;
            }
            col_nodes[c][b + 1] = ua;
        }

        /* Push each node down so no two share the same row */
        for (int j = 1; j < col_sz[c]; j++) {
            float needed = row[col_nodes[c][j - 1]] + 1.0f;
            if (row[col_nodes[c][j]] < needed)
                row[col_nodes[c][j]] = needed;
        }
    }

    /* ── Convert to pixel coordinates, centering each column independently ── */
    for (int c = 0; c <= max_col; c++) {
        if (col_sz[c] == 0) continue;

        /* Find min/max row value in this column */
        float rmin = row[col_nodes[c][0]], rmax = rmin;
        for (int j = 1; j < col_sz[c]; j++) {
            float r = row[col_nodes[c][j]];
            if (r < rmin) rmin = r;
            if (r > rmax) rmax = r;
        }
        float col_span_px = (rmax - rmin) * LAYOUT_STEP_Y;
        int   start_y     = (canvas_h - LAYOUT_BLOCK_H - (int)col_span_px) / 2;

        for (int j = 0; j < col_sz[c]; j++) {
            int u = col_nodes[c][j];
            out_x[u] = (lv_coord_t)(c * LAYOUT_STEP_X + 20);
            out_y[u] = (lv_coord_t)(start_y + (int)((row[u] - rmin) * LAYOUT_STEP_Y));
        }
    }
}

/* ─── Refresh (rebuild UI from state) ───────────────────────────────────────── */

void ui_pedalboard_refresh(void)
{
    if (!g_canvas_scroll) return;
    /* Remove all block children, redraw */
    lv_obj_clean(g_canvas_scroll);
    g_canvas = NULL; /* canvas removed — scroll events must reach g_canvas_scroll */

    /* Close any open connection panel before rebuilding the canvas */
    conn_panel_close();

    /* ── Collect I/O ports and cache globally ── */
    g_n_in_c  = collect_io_descs(true,  g_io_in_c,  16);
    g_n_out_c = collect_io_descs(false, g_io_out_c, 16);

    g_left_offset_c = (g_n_in_c > 0) ? IO_COL_W : 16;

    /* ── Compute plugin layout into global arrays ── */
    memset(g_layout_x, 0, sizeof(g_layout_x));
    memset(g_layout_y, 0, sizeof(g_layout_y));
    compute_layout(&g_pedalboard, g_layout_x, g_layout_y, UI_CANVAS_H);

    lv_coord_t max_right = 0;
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        g_layout_x[i] += (lv_coord_t)g_left_offset_c;
        lv_coord_t right = g_layout_x[i] + LAYOUT_BLOCK_W;
        if (right > max_right) max_right = right;
    }

    /* Leave 2 × LAYOUT_H_GAP so the connection square (centred at LAYOUT_H_GAP/2
     * past max_right) has enough clearance from the hardware output column. */
    g_right_col_x_c = (g_pedalboard.plugin_count > 0)
        ? (int)(max_right + 2 * LAYOUT_H_GAP)
        : g_left_offset_c;

    /* ── Draw connection lines + squares (behind everything) ── */
    redraw_connections();

    /* ── Draw left I/O column (inputs) ── */
    draw_io_column(g_canvas_scroll, g_io_in_c, g_n_in_c, 0, UI_CANVAS_H, false);

    /* Transparent tap zone over the left IO column — opens connection panel */
    if (g_n_in_c > 0) {
        lv_obj_t *io_tap = lv_obj_create(g_canvas_scroll);
        lv_obj_set_size(io_tap, g_left_offset_c, UI_CANVAS_H);
        lv_obj_set_pos(io_tap, 0, 0);
        lv_obj_set_style_bg_opa(io_tap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(io_tap, 0, 0);
        lv_obj_set_style_shadow_width(io_tap, 0, 0);
        lv_obj_clear_flag(io_tap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(io_tap, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(io_tap, on_io_col_tap, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)(-1));
    }

    /* ── Create plugin blocks (on top of connection lines) ── */
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        pb_plugin_t *plug = &g_pedalboard.plugins[i];
        lv_obj_t *block = ui_plugin_block_create(
            g_canvas_scroll, plug,
            on_block_tap, on_block_bypass, on_block_remove,
            (void *)(intptr_t)plug->instance_id);
        lv_obj_set_pos(block, g_layout_x[i], g_layout_y[i]);
    }

    /* ── Draw right I/O column (outputs) ── */
    draw_io_column(g_canvas_scroll, g_io_out_c, g_n_out_c,
                   g_right_col_x_c, UI_CANVAS_H, true);

    /* Transparent tap zone over the right IO column — selects system output as target */
    if (g_n_out_c > 0) {
        lv_obj_t *io_out_tap = lv_obj_create(g_canvas_scroll);
        lv_obj_set_size(io_out_tap, IO_COL_W, UI_CANVAS_H);
        lv_obj_set_pos(io_out_tap, g_right_col_x_c, 0);
        lv_obj_set_style_bg_opa(io_out_tap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(io_out_tap, 0, 0);
        lv_obj_set_style_shadow_width(io_out_tap, 0, 0);
        lv_obj_clear_flag(io_out_tap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(io_out_tap, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(io_out_tap, on_io_out_tap, LV_EVENT_SHORT_CLICKED, NULL);
    }

    /* ── Draw connection squares on top of everything ── */
    draw_conn_squares();

    /* ── Dynamic content dimensions ── */
    int content_w = g_right_col_x_c + IO_COL_W + 20;
    int content_h = UI_CANVAS_H;
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        int bot = (int)g_layout_y[i] + LAYOUT_BLOCK_H + 20;
        if (bot > content_h) content_h = bot;
    }

    lv_obj_t *spacer = lv_obj_create(g_canvas_scroll);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_pos(spacer, content_w - 1, content_h / 2);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* ── Scroll to leftmost position (inputs visible) ── */
    lv_obj_update_layout(g_canvas_scroll);
    lv_obj_scroll_to(g_canvas_scroll, 0, 0, LV_ANIM_OFF);
}

/* ─── Init ───────────────────────────────────────────────────────────────────── */

void ui_pedalboard_init(lv_obj_t *parent)
{
    g_parent_obj = parent;

    /* Reset canvas pointers — previous LVGL objects may have been freed
     * by lv_obj_clean(content_area) during screen transitions. */
    g_canvas_scroll = NULL;
    g_canvas        = NULL;
    g_conn_panel    = NULL; /* freed with content area */

    /* Scrollable container */
    g_canvas_scroll = lv_obj_create(parent);
    lv_obj_set_size(g_canvas_scroll, LV_PCT(100), LV_PCT(100));
    lv_obj_align(g_canvas_scroll, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(g_canvas_scroll, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(g_canvas_scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_canvas_scroll, 0, 0);
    lv_obj_set_style_pad_all(g_canvas_scroll, 0, 0);
    lv_obj_set_style_radius(g_canvas_scroll, 0, 0);
    lv_obj_add_flag(g_canvas_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_canvas_scroll, LV_DIR_ALL);
    /* NOTE: do NOT call set_content_width/height here — those resize the object
     * itself, not the scroll range.  The scroll range is determined by child
     * extents.  A transparent spacer added in ui_pedalboard_refresh() defines
     * the scrollable area. */

    if (g_pb_loaded) {
        ui_pedalboard_refresh();
    } else {
        /* Show placeholder */
        lv_obj_t *lbl = lv_label_create(g_canvas_scroll);
        lv_label_set_text(lbl, TR(TR_PB_EMPTY_MSG));
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
}

/* ─── Load / Save ────────────────────────────────────────────────────────────── */

/* Convert a TTL port URI (ingen:tail / ingen:head) to a mod-host Jack port name.
 *
 * sord resolves relative URIs against the TTL base, producing absolute file://
 * URIs relative to the bundle directory. Examples:
 *   file:///path/bundle/plugin_sym/port_sym  →  effect_<id>:port_sym
 *   file:///path/bundle/capture_1            →  system:capture_1
 *   file:///path/bundle/playback_1           →  system:playback_1
 *   file:///path/bundle/midi_capture_1       →  system:midi_capture_1
 *
 * Returns true if conversion succeeded. */
static bool uri_to_jack_port(const char *uri, const pedalboard_t *pb,
                              char *out, size_t outsz)
{
    /* Build the file:// prefix for the bundle dir to strip */
    char bundle_prefix[PB_PATH_MAX + 7];
    snprintf(bundle_prefix, sizeof(bundle_prefix), "file://%s/", pb->path);
    size_t prefix_len = strlen(bundle_prefix);

    const char *rel;
    if (strncmp(uri, bundle_prefix, prefix_len) == 0) {
        rel = uri + prefix_len;
    } else {
        /* Fallback: strip file:// and find the last two path components */
        const char *path = (strncmp(uri, "file://", 7) == 0) ? uri + 7 : uri;
        /* Try to strip bundle path without file:// */
        size_t blen = strlen(pb->path);
        if (strncmp(path, pb->path, blen) == 0 && path[blen] == '/')
            rel = path + blen + 1;
        else
            return false;
    }

    /* rel is now "plugin_sym/port_sym" or "hardware_port" */
    const char *slash = strchr(rel, '/');
    if (!slash) {
        /* Single component — hardware port: capture_N, playback_N, midi_* */
        snprintf(out, outsz, "system:%s", rel);
        /* Redirect audio captures through the pre-fx noise gate when loaded.
         * system:capture_1 → effect_9993:Output_1, etc. */
        if (pre_fx_is_loaded()
                && strncmp(rel, "capture_", 8) == 0
                && rel[8] >= '1' && rel[8] <= '8') {
            snprintf(out, outsz, "effect_%d:Output_%c",
                     PRE_FX_GATE_INSTANCE, rel[8]);
        }
        return true;
    }

    /* Two components: plugin_sym / port_sym */
    char plugin_sym[PB_SYMBOL_MAX];
    size_t sym_len = (size_t)(slash - rel);
    if (sym_len >= sizeof(plugin_sym)) return false;
    memcpy(plugin_sym, rel, sym_len);
    plugin_sym[sym_len] = '\0';

    const char *port_sym = slash + 1;

    /* Look up instance_id by symbol */
    for (int i = 0; i < pb->plugin_count; i++) {
        if (strcmp(pb->plugins[i].symbol, plugin_sym) == 0) {
            snprintf(out, outsz, "effect_%d:%s", pb->plugins[i].instance_id, port_sym);
            return true;
        }
    }
    return false;
}

/* ─── Last-state persistence ─────────────────────────────────────────────────── */

static void last_state_save(void)
{
    if (!g_pb_loaded) return;
    mpt_settings_t *s = settings_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "pedalboard", g_pedalboard.path);
    cJSON_AddNumberToObject(root, "snapshot",   g_pedalboard.current_snapshot);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return;

    FILE *f = fopen(s->last_state_file, "w");
    if (f) { fputs(json, f); fclose(f); }
    free(json);
}

void ui_pedalboard_save_last_state(void)
{
    last_state_save();
}

/* ─── Apply snapshot to mod-host and refresh UI ──────────────────────────────── */

void ui_pedalboard_apply_snapshot(int idx)
{
    if (!g_pb_loaded) return;
    if (idx < 0 || idx >= g_pedalboard.snapshot_count) return;

    pb_snapshot_load(&g_pedalboard, idx);

    pb_snapshot_t *snap = &g_pedalboard.snapshots[idx];
    for (int i = 0; i < snap->plugin_count; i++) {
        snap_plugin_t *sp = &snap->plugins[i];
        pb_plugin_t *plug = pb_find_plugin_by_symbol(&g_pedalboard, sp->symbol);
        if (!plug) continue;
        host_bypass(plug->instance_id, sp->bypassed);
        for (int j = 0; j < sp->param_count; j++)
            host_param_set(plug->instance_id, sp->params[j].symbol, sp->params[j].value);
        for (int j = 0; j < sp->patch_param_count; j++)
            if (sp->patch_params[j].path[0])
                host_patch_set(plug->instance_id,
                               sp->patch_params[j].uri,
                               sp->patch_params[j].path);
    }
    /* Marshal LVGL call to main thread (may be called from background thread). */
    lv_async_call(pb_snapshot_ui_refresh, NULL);
}

static void pb_snapshot_ui_refresh(void *arg)
{
    (void)arg;
    ui_snapshot_bar_refresh();
    last_state_save();
}

void ui_pedalboard_load(const char *bundle_path,
                        pb_progress_cb_t progress_cb, void *progress_ud)
{
    pb_init(&g_pedalboard);
    fprintf(stderr, "[ui_pedalboard] loading bundle: %s\n", bundle_path);
    if (pb_load(&g_pedalboard, bundle_path) < 0) {
        fprintf(stderr, "[ui_pedalboard] pb_load failed\n");
        ui_app_show_toast_error(TR(TR_MSG_PB_LOAD_ERROR));
        return;
    }
    fprintf(stderr, "[ui_pedalboard] loaded '%s': %d plugins, %d connections\n",
            g_pedalboard.name, g_pedalboard.plugin_count, g_pedalboard.connection_count);
    g_pb_loaded = true;

    /* ── Rebuild mod-host state ── */

    /* 1. Clear all existing plugins */
    host_remove_all();

    /* Reload pre-fx instances (host_remove_all removes them too) */
    pre_fx_reload();

    /* 2. Add plugins, set bypass and port values */
    int total_plugins = g_pedalboard.plugin_count;
    for (int i = 0; i < total_plugins; i++) {
        pb_plugin_t *plug = &g_pedalboard.plugins[i];
        plug->instance_id = i; /* sequential instance IDs */

        if (host_add_plugin(plug->instance_id, plug->uri) < 0) {
            fprintf(stderr, "[pedalboard] Failed to add plugin %s\n", plug->uri);
            if (progress_cb) progress_cb(i + 1, total_plugins, progress_ud);
            continue;
        }

        if (!plug->enabled)
            host_bypass(plug->instance_id, true);

        if (plug->preset_uri[0])
            host_preset_load(plug->instance_id, plug->preset_uri);

        for (int j = 0; j < plug->port_count; j++) {
            pb_port_t *port = &plug->ports[j];
            host_param_set(plug->instance_id, port->symbol, port->value);
        }

        /* Patch parameters (file paths) */
        for (int j = 0; j < plug->patch_param_count; j++) {
            pb_patch_t *pp = &plug->patch_params[j];
            if (pp->path[0])
                host_patch_set(plug->instance_id, pp->uri, pp->path);
        }

        /* MIDI CC bindings */
        for (int j = 0; j < plug->port_count; j++) {
            pb_port_t *port = &plug->ports[j];
            if (port->midi_channel >= 0 && port->midi_cc >= 0) {
                host_midi_map(plug->instance_id, port->symbol,
                              port->midi_channel, port->midi_cc,
                              port->midi_min, port->midi_max);
            }
        }

        if (progress_cb) progress_cb(i + 1, total_plugins, progress_ud);
    }

    /* 3. Make audio/MIDI connections */
    if (progress_cb) progress_cb(-1, 0, progress_ud); /* phase: connections */
    for (int i = 0; i < g_pedalboard.connection_count; i++) {
        pb_connection_t *conn = &g_pedalboard.connections[i];
        char from[256], to[256];
        if (uri_to_jack_port(conn->from, &g_pedalboard, from, sizeof(from)) &&
            uri_to_jack_port(conn->to,   &g_pedalboard, to,   sizeof(to))) {
            host_connect(from, to);
        } else {
            fprintf(stderr, "[pedalboard] Cannot resolve connection: %s → %s\n",
                    conn->from, conn->to);
        }
    }

    /* Note: host_state_load() was removed.  All control port values are sent
     * individually via host_param_set() above, and file paths via
     * host_patch_set().  host_state_load() was redundant and consistently
     * timed out (10 s) on Pi hardware, blocking the splash screen. */

    /* Marshal LVGL calls to the main thread — ui_pedalboard_load() may be
     * called from a background thread (boot auto-load).  Touching LVGL
     * objects from a non-LVGL thread causes lv_inv_area assertions and
     * potential data-structure corruption.  lv_async_call is thread-safe. */
    lv_async_call(pb_load_ui_refresh, NULL);
}

/* Async callback — runs on the LVGL main thread after ui_pedalboard_load(). */
static void pb_load_ui_refresh(void *arg)
{
    (void)arg;
    ui_app_update_title(g_pedalboard.name, false);
    ui_pedalboard_refresh();
    last_state_save();
}

void ui_pedalboard_save(void)
{
    if (!g_pb_loaded) return;
    if (pb_save(&g_pedalboard) < 0) {
        ui_app_show_toast_error(TR(TR_MSG_PB_SAVE_FAIL));
        return;
    }
    ui_app_update_title(g_pedalboard.name, false);
}

void ui_pedalboard_save_snapshot(void)
{
    if (!g_pb_loaded) return;
    int idx = g_pedalboard.current_snapshot;
    if (idx < 0 || idx >= g_pedalboard.snapshot_count) return;

    pb_snapshot_overwrite(&g_pedalboard, idx);

    /* Save only snapshots.json (not the full TTL) */
    char snap_path[1024];
    snprintf(snap_path, sizeof(snap_path), "%s/snapshots.json", g_pedalboard.path);
    if (snapshot_save(snap_path, g_pedalboard.snapshots,
                      g_pedalboard.snapshot_count,
                      g_pedalboard.current_snapshot) < 0) {
        ui_app_show_toast_error(TR(TR_MSG_SNAP_SAVE_FAIL));
        return;
    }
    g_pedalboard.modified = false;
    ui_app_update_title(g_pedalboard.name, false);
}

void ui_pedalboard_delete_snapshot(void)
{
    if (!g_pb_loaded) return;
    int idx = g_pedalboard.current_snapshot;
    if (idx < 0 || idx >= g_pedalboard.snapshot_count) return;

    pb_snapshot_delete(&g_pedalboard, idx);

    char snap_path[1024];
    snprintf(snap_path, sizeof(snap_path), "%s/snapshots.json", g_pedalboard.path);
    snapshot_save(snap_path, g_pedalboard.snapshots,
                  g_pedalboard.snapshot_count,
                  g_pedalboard.current_snapshot);

    ui_snapshot_bar_refresh();
    ui_app_update_title(g_pedalboard.name, g_pedalboard.modified);
}

void ui_pedalboard_delete(void)
{
    if (!g_pb_loaded) return;

    char bundle_path[PB_PATH_MAX];
    snprintf(bundle_path, sizeof(bundle_path), "%s", g_pedalboard.path);

    /* Unload from mod-host before removing files */
    host_remove_all();

    pb_init(&g_pedalboard);
    g_pb_loaded = false;

    pb_bundle_delete(bundle_path);

    ui_pedalboard_refresh();
    ui_app_update_title(TR(TR_NO_PEDALBOARD), false);
}

void ui_pedalboard_chain_bypass(bool bypass_all)
{
    if (!g_pb_loaded) return;
    pthread_mutex_lock(&g_pb_mutex);
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        pb_plugin_t *plug = &g_pedalboard.plugins[i];
        host_bypass(plug->instance_id, bypass_all ? true : !plug->enabled);
    }
    pthread_mutex_unlock(&g_pb_mutex);
}

/* Async callback: update param editor on the main LVGL thread. */
typedef struct { char sym[PB_SYMBOL_MAX]; float val; } param_upd_t;

static void param_editor_async_cb(void *arg)
{
    param_upd_t *upd = arg;
    ui_param_editor_update(upd->sym, upd->val);
    free(upd);
}

void ui_pedalboard_update_param(int instance_id, const char *symbol, float value)
{
    /* Called from the feedback thread — protect g_pedalboard with the mutex. */
    if (!g_pb_loaded) return;

    bool found = false;
    pthread_mutex_lock(&g_pb_mutex);
    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (plug) {
        for (int i = 0; i < plug->port_count; i++) {
            if (strcmp(plug->ports[i].symbol, symbol) == 0) {
                plug->ports[i].value = value;
                found = true;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_pb_mutex);

    /* ui_param_editor_update touches LVGL — schedule on the main thread. */
    if (found) {
        param_upd_t *upd = malloc(sizeof(*upd));
        if (upd) {
            snprintf(upd->sym, sizeof(upd->sym), "%s", symbol);
            upd->val = value;
            lv_async_call(param_editor_async_cb, upd);
        }
    }
}

/* ─── MIDI mapped ─────────────────────────────────────────────────────────────── */

typedef struct {
    int   instance_id;
    char  symbol[PB_SYMBOL_MAX];
    int   ch, cc;
    float min, max;
} midi_mapped_t;

/* Update the pedalboard data model with a new MIDI mapping (thread-safe). */
static void pb_apply_midi_mapped(int instance_id, const char *symbol,
                                 int ch, int cc, float min, float max)
{
    if (!g_pb_loaded) return;
    pthread_mutex_lock(&g_pb_mutex);
    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (plug) {
        for (int i = 0; i < plug->port_count; i++) {
            if (strcmp(plug->ports[i].symbol, symbol) == 0) {
                plug->ports[i].midi_channel = ch;
                plug->ports[i].midi_cc      = cc;
                plug->ports[i].midi_min     = min;
                plug->ports[i].midi_max     = max;
                break;
            }
        }
        g_pedalboard.modified = true;
    }
    pthread_mutex_unlock(&g_pb_mutex);
}

/* Async callback: runs on LVGL main thread, updates param editor UI. */
static void midi_mapped_async_cb(void *arg)
{
    midi_mapped_t *m = arg;
    ui_param_editor_on_midi_mapped(m->instance_id, m->symbol,
                                   m->ch, m->cc, m->min, m->max);
    free(m);
}

/* Called from main.c feedback thread on "midi_mapped" message.
 * Schedules pedalboard data update and param editor refresh on LVGL thread. */
void ui_pedalboard_on_midi_mapped(int instance_id, const char *symbol,
                                  int ch, int cc, float min, float max)
{
    /* Update pedalboard data (mutex-safe from any thread) */
    pb_apply_midi_mapped(instance_id, symbol, ch, cc, min, max);

    /* Schedule param editor UI update on the LVGL main thread */
    midi_mapped_t *msg = malloc(sizeof(*msg));
    if (msg) {
        msg->instance_id = instance_id;
        snprintf(msg->symbol, sizeof(msg->symbol), "%s", symbol);
        msg->ch = ch; msg->cc = cc; msg->min = min; msg->max = max;
        lv_async_call(midi_mapped_async_cb, msg);
    }
}

/* Accessor for other modules */
pedalboard_t *ui_pedalboard_get(void) { return &g_pedalboard; }
bool          ui_pedalboard_is_loaded(void) { return g_pb_loaded; }
