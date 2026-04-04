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

static void on_block_tap(void *userdata)
{
    int instance_id = (int)(intptr_t)userdata;
    pb_plugin_t *plug = pb_find_plugin(&g_pedalboard, instance_id);
    if (!plug) return;

    ui_param_editor_show(instance_id, plug->label,
                         plug->ports, plug->port_count,
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

/* ─── Refresh (rebuild UI from state) ───────────────────────────────────────── */

void ui_pedalboard_refresh(void)
{
    if (!g_canvas_scroll) return;
    /* Remove all block children, redraw */
    lv_obj_clean(g_canvas_scroll);
    g_canvas = NULL;

    /* Re-create canvas for lines */
    g_canvas = lv_canvas_create(g_canvas_scroll);
    lv_obj_set_size(g_canvas, CANVAS_W, CANVAS_H);
    lv_obj_align(g_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x1A1A1A), LV_OPA_TRANSP);

    /* Create plugin blocks */
    for (int i = 0; i < g_pedalboard.plugin_count; i++) {
        pb_plugin_t *plug = &g_pedalboard.plugins[i];
        lv_obj_t *block = ui_plugin_block_create(
            g_canvas_scroll, plug,
            on_block_tap, on_block_bypass, on_block_remove,
            (void *)(intptr_t)plug->instance_id);
        lv_obj_set_pos(block, (lv_coord_t)plug->canvas_x, (lv_coord_t)plug->canvas_y);
    }

    redraw_connections();
}

/* ─── Init ───────────────────────────────────────────────────────────────────── */

void ui_pedalboard_init(lv_obj_t *parent)
{
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
 * TTL stores full URIs like:
 *   file:///path/to/pb.ttl/plugin_sym/port_sym  →  effect_<id>:port_sym
 *   file:///path/to/pb.ttl/capture_1            →  system:capture_1
 *   file:///path/to/pb.ttl/playback_1           →  system:playback_1
 *   file:///path/to/pb.ttl/midi_capture_1       →  system:midi_capture_1
 *   file:///path/to/pb.ttl/midi_playback_1      →  system:midi_playback_1
 *
 * Returns true if conversion succeeded. */
static bool uri_to_jack_port(const char *uri, const pedalboard_t *pb,
                              char *out, size_t outsz)
{
    /* Strip scheme + host to get the path portion after the .ttl file */
    const char *ttl_marker = strstr(uri, ".ttl");
    if (!ttl_marker) return false;
    /* Advance past ".ttl" and the following "/" */
    const char *after_ttl = ttl_marker + 4;
    if (*after_ttl == '/') after_ttl++;

    /* Split into at most two components: [plugin_sym/]port_sym */
    const char *slash = strchr(after_ttl, '/');
    if (!slash) {
        /* Single component — hardware port */
        const char *port_name = after_ttl;
        snprintf(out, outsz, "system:%s", port_name);
        return true;
    }

    /* Two components: plugin_sym / port_sym */
    char plugin_sym[PB_SYMBOL_MAX];
    size_t sym_len = (size_t)(slash - after_ttl);
    if (sym_len >= sizeof(plugin_sym)) return false;
    memcpy(plugin_sym, after_ttl, sym_len);
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
    /* Save current if modified */
    if (g_pb_loaded && g_pedalboard.modified) {
        pb_save(&g_pedalboard);
    }

    pb_init(&g_pedalboard);
    if (pb_load(&g_pedalboard, bundle_path) < 0) {
        ui_app_show_message("Error", "Failed to load pedalboard.", 0);
        return;
    }
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
