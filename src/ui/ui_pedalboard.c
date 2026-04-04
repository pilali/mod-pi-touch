#include "ui_pedalboard.h"
#include "ui_app.h"
#include "ui_param_editor.h"
#include "ui_plugin_block.h"
#include "../pedalboard.h"
#include "../host_comm.h"
#include "../settings.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── Global pedalboard state ────────────────────────────────────────────────── */
static pedalboard_t g_pedalboard;
static bool         g_pb_loaded = false;

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

/* ─── Connection drawing ─────────────────────────────────────────────────────── */

static void redraw_connections(void)
{
    if (!g_canvas) return;
    /* Clear canvas */
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x1A1A1A), LV_OPA_TRANSP);

    /* Draw a line for each connection */
    /* In LVGL 9, we use lv_line on the scroll container for each arc */
    /* For simplicity, connections are drawn as lv_line objects; they are
     * recreated on every refresh. */
    /* TODO: implement Bézier curves via canvas draw */
}

/* ─── Block event handlers ───────────────────────────────────────────────────── */

static void on_block_bypass(void *userdata);  /* forward declaration */

static void on_block_tap(void *userdata)
{
    int instance_id = (int)(intptr_t)userdata;
    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (!plug) return;

    ui_param_editor_show(instance_id, plug->label,
                         plug->ports, plug->port_count,
                         plug->enabled,
                         on_block_bypass, (void *)(intptr_t)instance_id,
                         NULL, NULL);
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

#define LAYOUT_BLOCK_W  160
#define LAYOUT_BLOCK_H   80
#define LAYOUT_H_GAP     80   /* horizontal gap between columns */
#define LAYOUT_V_GAP     24   /* vertical gap between rows in same column */
#define LAYOUT_STEP_X   (LAYOUT_BLOCK_W + LAYOUT_H_GAP)
#define LAYOUT_STEP_Y   (LAYOUT_BLOCK_H + LAYOUT_V_GAP)
#define LAYOUT_MAX_COLS  32
#define LAYOUT_MAX_ADJ   16   /* max successors/predecessors per plugin */

/* ─── I/O port indicators ────────────────────────────────────────────────────── */

#define IO_DOT       18   /* circle/square diameter */
#define IO_ROW_H     (IO_DOT + 14) /* vertical step between port indicators */
#define IO_COL_W     72   /* width reserved on each side for I/O column */
#define IO_LINE_X    26   /* x of the vertical bar within the I/O column */
#define IO_LINE_W     2   /* thickness of the vertical bar */

typedef struct {
    char label[16]; /* "In 1", "In 2", "MIDI", "Out 1"… */
    bool is_midi;
} io_port_desc_t;

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
static int collect_sysports(const pedalboard_t *pb, bool want_inputs,
                              io_port_desc_t *descs, int max)
{
    int count = 0;
    /* Gather unique names */
    char names[16][64]; int n_names = 0;

    for (int c = 0; c < pb->connection_count && n_names < 16; c++) {
        const char *sp = want_inputs
            ? uri_to_sysport(pb->connections[c].from, pb)
            : uri_to_sysport(pb->connections[c].to,   pb);
        if (!sp) continue;
        /* Only inputs have "capture", outputs have "playback" */
        bool is_cap  = (strstr(sp, "capture")  != NULL);
        bool is_play = (strstr(sp, "playback") != NULL);
        if (want_inputs  && !is_cap)  continue;
        if (!want_inputs && !is_play) continue;
        /* Deduplicate */
        bool dup = false;
        for (int k = 0; k < n_names; k++) if (strcmp(names[k], sp) == 0) { dup = true; break; }
        if (!dup) snprintf(names[n_names++], 64, "%s", sp);
    }

    /* Sort: audio first (no "midi" prefix), then MIDI */
    /* Simple bubble sort */
    for (int a = 0; a < n_names - 1; a++)
        for (int b = a+1; b < n_names; b++) {
            bool a_midi = (strncmp(names[a], "midi", 4) == 0);
            bool b_midi = (strncmp(names[b], "midi", 4) == 0);
            if (!a_midi && !b_midi) continue; /* both audio — keep order */
            if (a_midi && !b_midi) { char tmp[64]; memcpy(tmp,names[a],64); memcpy(names[a],names[b],64); memcpy(names[b],tmp,64); }
        }

    for (int i = 0; i < n_names && count < max; i++) {
        bool is_midi = (strncmp(names[i], "midi", 4) == 0);
        const char *num = strrchr(names[i], '_');
        if (is_midi)
            snprintf(descs[count].label, sizeof(descs[0].label), "MIDI");
        else if (want_inputs)
            snprintf(descs[count].label, sizeof(descs[0].label), "In %s",  num ? num+1 : "?");
        else
            snprintf(descs[count].label, sizeof(descs[0].label), "Out %s", num ? num+1 : "?");
        descs[count].is_midi = is_midi;
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

    /* ── Collect I/O ports ── */
    io_port_desc_t io_in[16], io_out[16];
    int n_in  = collect_sysports(&g_pedalboard, true,  io_in,  16);
    int n_out = collect_sysports(&g_pedalboard, false, io_out, 16);

    int left_offset = (n_in > 0) ? IO_COL_W : 16;

    /* ── Compute plugin layout, then shift right to leave room for inputs ── */
    lv_coord_t layout_x[PB_MAX_PLUGINS] = {0};
    lv_coord_t layout_y[PB_MAX_PLUGINS] = {0};
    compute_layout(&g_pedalboard, layout_x, layout_y, UI_CANVAS_H);

    lv_coord_t max_right = 0;
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        layout_x[i] += (lv_coord_t)left_offset;
        lv_coord_t right = layout_x[i] + LAYOUT_BLOCK_W;
        if (right > max_right) max_right = right;
    }

    /* ── Draw left I/O column (inputs) ── */
    draw_io_column(g_canvas_scroll, io_in, n_in, 0, UI_CANVAS_H, false);

    /* ── Create plugin blocks ── */
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        pb_plugin_t *plug = &g_pedalboard.plugins[i];
        lv_obj_t *block = ui_plugin_block_create(
            g_canvas_scroll, plug,
            on_block_tap, on_block_bypass, on_block_remove,
            (void *)(intptr_t)plug->instance_id);
        lv_obj_set_pos(block, layout_x[i], layout_y[i]);
    }

    /* ── Draw right I/O column (outputs) ── */
    int right_col_x = (g_pedalboard.plugin_count > 0)
        ? (int)(max_right + LAYOUT_H_GAP / 2)
        : left_offset;
    draw_io_column(g_canvas_scroll, io_out, n_out, right_col_x, UI_CANVAS_H, true);

    redraw_connections();

    /* ── Scroll to leftmost position (inputs visible) ── */
    lv_obj_update_layout(g_canvas_scroll);
    lv_obj_scroll_to(g_canvas_scroll, 0, 0, LV_ANIM_OFF);
}

/* ─── Init ───────────────────────────────────────────────────────────────────── */

void ui_pedalboard_init(lv_obj_t *parent)
{
    /* Reset canvas pointers — previous LVGL objects may have been freed
     * by lv_obj_clean(content_area) during screen transitions. */
    g_canvas_scroll = NULL;
    g_canvas        = NULL;

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
    lv_obj_set_content_width(g_canvas_scroll, CANVAS_W);
    lv_obj_set_content_height(g_canvas_scroll, CANVAS_H);

    if (g_pb_loaded) {
        ui_pedalboard_refresh();
    } else {
        /* Show placeholder */
        lv_obj_t *lbl = lv_label_create(g_canvas_scroll);
        lv_label_set_text(lbl, "No pedalboard loaded.\nTap 'Banks' to open one.");
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

void ui_pedalboard_load(const char *bundle_path)
{
    pb_init(&g_pedalboard);
    fprintf(stderr, "[ui_pedalboard] loading bundle: %s\n", bundle_path);
    if (pb_load(&g_pedalboard, bundle_path) < 0) {
        fprintf(stderr, "[ui_pedalboard] pb_load failed\n");
        ui_app_show_message("Error", "Failed to load pedalboard.", 0);
        return;
    }
    fprintf(stderr, "[ui_pedalboard] loaded '%s': %d plugins, %d connections\n",
            g_pedalboard.name, g_pedalboard.plugin_count, g_pedalboard.connection_count);
    g_pb_loaded = true;

    /* ── Rebuild mod-host state ── */

    /* 1. Clear all existing plugins */
    host_remove_all();

    /* 2. Add plugins, set bypass and port values */
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        pb_plugin_t *plug = &g_pedalboard.plugins[i];
        plug->instance_id = i; /* sequential instance IDs */

        if (host_add_plugin(plug->instance_id, plug->uri) < 0) {
            fprintf(stderr, "[pedalboard] Failed to add plugin %s\n", plug->uri);
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

        /* MIDI CC bindings */
        for (int j = 0; j < plug->port_count; j++) {
            pb_port_t *port = &plug->ports[j];
            if (port->midi_channel >= 0 && port->midi_cc >= 0) {
                host_midi_map(plug->instance_id, port->symbol,
                              port->midi_channel, port->midi_cc,
                              port->midi_min, port->midi_max);
            }
        }
    }

    /* 3. Make audio/MIDI connections */
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

    /* 4. Restore LV2 plugin state (internal state beyond control ports) */
    host_state_load(bundle_path);

    ui_app_update_title(g_pedalboard.name, false);
    ui_pedalboard_refresh();
}

void ui_pedalboard_save(void)
{
    if (!g_pb_loaded) return;
    if (pb_save(&g_pedalboard) < 0) {
        ui_app_show_message("Error", "Failed to save pedalboard.", 0);
        return;
    }
    ui_app_update_title(g_pedalboard.name, false);
}

void ui_pedalboard_update_param(int instance_id, const char *symbol, float value)
{
    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (!plug) return;
    for (int i = 0; i < plug->port_count; i++) {
        if (strcmp(plug->ports[i].symbol, symbol) == 0) {
            plug->ports[i].value = value;
            ui_param_editor_update(symbol, value);
            break;
        }
    }
}

/* Accessor for other modules */
pedalboard_t *ui_pedalboard_get(void) { return &g_pedalboard; }
bool          ui_pedalboard_is_loaded(void) { return g_pb_loaded; }
