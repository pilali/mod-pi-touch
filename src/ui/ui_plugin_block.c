#include "ui_plugin_block.h"
#include "ui_pedalboard.h"
#include "ui_app.h"
#include "../i18n.h"
#include "../plugin_manager.h"
#include "../widget_prefs.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

/* ── Block gradient helper ───────────────────────────────────────────────────── */
/* Apply a vertical gradient with a ~10° rightward tilt to the block background.
 * The gradient runs from a slightly lighter top to a slightly darker bottom,
 * giving depth without overwhelming the accent colour. */
static void apply_block_bg(lv_obj_t *block, lv_color_t bg)
{
    /* Vertical gradient: slightly lighter at top, slightly darker at bottom.
     * Colors stored as values by LVGL (no pointer lifetime issue). */
    lv_obj_set_style_bg_color(block,
        lv_color_mix(lv_color_white(), bg, 28), 0);   /* ~11% lighter */
    lv_obj_set_style_bg_grad_color(block,
        lv_color_mix(lv_color_black(), bg, 28), 0);   /* ~11% darker  */
    lv_obj_set_style_bg_grad_dir(block, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
}

/* ── Block size constants ────────────────────────────────────────────────────── */

/* Single-width = LAYOUT_BLOCK_W (160), double-width spans 2 grid slots (400) */
#define BLOCK_W_1X  LAYOUT_BLOCK_W
#define BLOCK_W_2X  (LAYOUT_BLOCK_W + LAYOUT_H_GAP + LAYOUT_BLOCK_W)

/* Height is fixed at one grid slot for now */
#define BLOCK_H     LAYOUT_BLOCK_H

/* Layout inside a block */
#define BLOCK_PAD   6   /* px padding on each side */
#define NAME_H     14   /* px reserved for the plugin name label */
#define NAME_GAP    4   /* px gap between name and control grid */
#define CELL_GAP    6   /* px gap between control cells */

/* Arc knob display cap (consistent across all cell sizes) */
#define ARC_MAX_D  40   /* max arc diameter in px */
#define ARC_MIN_D  18   /* min readable arc diameter */

/* ── Category → accent colour ───────────────────────────────────────────────── */

lv_color_t ui_plugin_block_category_color(const char *cat)
{
    if (!cat || !cat[0]) return UI_COLOR_PRIMARY;
    if (!strcmp(cat,"Distortion") || !strcmp(cat,"Waveshaper") || !strcmp(cat,"Simulator"))
        return lv_color_hex(0xFF6B6B);
    if (!strcmp(cat,"Reverb") || !strcmp(cat,"Spatial") || !strcmp(cat,"Ambisonics"))
        return lv_color_hex(0xA78BFA);
    if (!strcmp(cat,"Delay") || !strcmp(cat,"Echo"))
        return lv_color_hex(0x60A5FA);
    if (!strcmp(cat,"Chorus") || !strcmp(cat,"Flanger") ||
        !strcmp(cat,"Phaser")  || !strcmp(cat,"Modulator"))
        return lv_color_hex(0xF472B6);
    if (!strcmp(cat,"EQ")       || !strcmp(cat,"Multi EQ") || !strcmp(cat,"Para EQ") ||
        !strcmp(cat,"Filter")   || !strcmp(cat,"Highpass")  || !strcmp(cat,"Lowpass") ||
        !strcmp(cat,"Allpass")  || !strcmp(cat,"Bandpass")  || !strcmp(cat,"Comb")    ||
        !strcmp(cat,"Spectral"))
        return lv_color_hex(0xFBBF24);
    if (!strcmp(cat,"Compressor") || !strcmp(cat,"Limiter") ||
        !strcmp(cat,"Dynamics")   || !strcmp(cat,"Expander") ||
        !strcmp(cat,"Gate")       || !strcmp(cat,"Envelope"))
        return lv_color_hex(0x34D399);
    if (!strcmp(cat,"Pitch"))
        return lv_color_hex(0xFB923C);
    if (!strcmp(cat,"Generator") || !strcmp(cat,"Instrument") ||
        !strcmp(cat,"Oscillator") || !strcmp(cat,"Constant"))
        return lv_color_hex(0x22D3EE);
    if (!strcmp(cat,"Amplifier"))
        return lv_color_hex(0xE2B96A);
    return UI_COLOR_PRIMARY;
}

/* ── Per-cell and per-block context structs ──────────────────────────────────── */

#define BLOCK_MAX_CELLS     8
#define CELL_ENUM_LBL_MAX  32

typedef struct block_ctx_s block_ctx_t; /* forward decl */

typedef struct {
    block_ctx_t *bc;
    char         symbol[PB_SYMBOL_MAX];
    float        min, max, value;
    bool         is_toggle, is_enum, is_integer;
    bool         updating;   /* true while set_param is writing — suppresses arc callback */
    int          enum_count;
    float        enum_values[16];
    char         enum_labels[16][CELL_ENUM_LBL_MAX];
    lv_obj_t    *indicator;  /* dot / val_lbl / arc */
    lv_color_t   accent;
} cell_ctx_t;

struct block_ctx_s {
    block_cb_t       on_tap;
    block_cb_t       on_bypass;
    block_cb_t       on_remove;
    block_param_cb_t on_param;
    void            *userdata;
    bool             bypassed;
    lv_color_t       accent;
    int              cell_count;
    cell_ctx_t       cells[BLOCK_MAX_CELLS];
};

/* ── Cell interaction callbacks ──────────────────────────────────────────────── */

/* Long press on any cell → open param editor */
static void cell_long_press_cb(lv_event_t *e)
{
    cell_ctx_t *cc = lv_event_get_user_data(e);
    if (ui_pedalboard_intercept_plugin_click((int)(intptr_t)cc->bc->userdata))
        return;
    if (cc->bc->on_tap) cc->bc->on_tap(cc->bc->userdata);
}

/* Toggle cell: tap to flip 0↔1 */
static void cell_toggle_cb(lv_event_t *e)
{
    cell_ctx_t *cc = lv_event_get_user_data(e);
    cc->value = (cc->value > 0.5f) ? 0.0f : 1.0f;
    bool active = cc->value > 0.5f;
    if (cc->indicator) {
        lv_obj_set_style_bg_color(cc->indicator,
            active ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
        lv_obj_set_style_shadow_color(cc->indicator,
            active ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
        lv_obj_set_style_shadow_width(cc->indicator, active ? 8 : 0, 0);
    }
    if (cc->bc->on_param)
        cc->bc->on_param(cc->bc->userdata, cc->symbol, cc->value);
}

/* Enum cell: tap to cycle to next value */
static void cell_enum_cb(lv_event_t *e)
{
    cell_ctx_t *cc = lv_event_get_user_data(e);
    if (cc->enum_count <= 0) return;
    int cur = 0;
    for (int k = 0; k < cc->enum_count; k++)
        if (fabsf(cc->enum_values[k] - cc->value) < 0.5f) { cur = k; break; }
    cur = (cur + 1) % cc->enum_count;
    cc->value = cc->enum_values[cur];
    if (cc->indicator) lv_label_set_text(cc->indicator, cc->enum_labels[cur]);
    if (cc->bc->on_param)
        cc->bc->on_param(cc->bc->userdata, cc->symbol, cc->value);
}

/* Arc cell: drag to adjust */
static void cell_arc_cb(lv_event_t *e)
{
    cell_ctx_t *cc = lv_event_get_user_data(e);
    if (!cc->indicator || cc->updating) return;
    int arc_val = lv_arc_get_value(cc->indicator);
    float norm = (float)arc_val / 1000.0f;
    float v = cc->min + (cc->max - cc->min) * norm;
    if (cc->is_integer) v = roundf(v);
    if (v < cc->min) v = cc->min;
    if (v > cc->max) v = cc->max;
    cc->value = v;
    if (cc->bc->on_param)
        cc->bc->on_param(cc->bc->userdata, cc->symbol, cc->value);
}

static void block_delete_cb(lv_event_t *e)
{
    lv_obj_t *block = lv_event_get_target(e);
    block_ctx_t *ctx = lv_obj_get_user_data(block);
    free(ctx);
}

/* Long press on block background → open param editor */
static void block_open_editor_cb(lv_event_t *e)
{
    block_ctx_t *ctx = lv_event_get_user_data(e);
    if (ui_pedalboard_intercept_plugin_click((int)(intptr_t)ctx->userdata))
        return;
    if (ctx->on_tap) ctx->on_tap(ctx->userdata);
}

/* ── Block sizing helpers ────────────────────────────────────────────────────── */

static int displayed_ctrl_count(const pb_plugin_t *plug)
{
    /* Custom selection overrides default port count */
    char custom[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX];
    int n = widget_prefs_get(plug->symbol, custom);
    if (n > 0) return n;

    const pm_plugin_info_t *pi = pm_plugin_by_uri(plug->uri);
    if (!pi) return 0;
    return (pi->modgui_port_count > 0) ? pi->modgui_port_count : pi->ctrl_in_count;
}

int ui_plugin_block_width(const pb_plugin_t *plug)
{
    return (displayed_ctrl_count(plug) > 4) ? BLOCK_W_2X : BLOCK_W_1X;
}

int ui_plugin_block_height(const pb_plugin_t *plug)
{
    (void)plug;
    return BLOCK_H;
}

/* ── Control cell rendering ──────────────────────────────────────────────────── */

/* Find the current value for a port symbol in pb_plugin_t; returns default if not found. */
static float port_value(const pb_plugin_t *plug, const pm_port_info_t *pm_port)
{
    for (int j = 0; j < plug->port_count; j++) {
        if (strcmp(plug->ports[j].symbol, pm_port->symbol) == 0)
            return plug->ports[j].value;
    }
    return pm_port->default_val;
}

/* Render a single control cell at (cx, cy). Fills *cc with cell context.
 * All children created on `block` with absolute positioning. */
static void create_ctrl_cell(lv_obj_t *block,
                              int cx, int cy, int cw, int ch,
                              const pm_port_info_t *pm_port,
                              float value,
                              lv_color_t accent,
                              cell_ctx_t *cc)
{
    /* Fill cell context (skip if over pool limit — cell will be visual-only) */
    if (cc) {
        snprintf(cc->symbol, sizeof(cc->symbol), "%s", pm_port->symbol);
        cc->min        = pm_port->min;
        cc->max        = pm_port->max;
        cc->value      = value;
        cc->is_toggle  = pm_port->toggled;
        cc->is_enum    = (pm_port->enumeration && pm_port->enum_count > 0);
        cc->is_integer = pm_port->integer;
        cc->enum_count = pm_port->enum_count < 16 ? pm_port->enum_count : 16;
        cc->accent     = accent;
        for (int k = 0; k < cc->enum_count; k++) {
            cc->enum_values[k] = pm_port->enum_values[k];
            snprintf(cc->enum_labels[k], CELL_ENUM_LBL_MAX,
                     "%s", pm_port->enum_labels[k]);
        }
    }

    /* ── Port name label (non-interactive) ── */
    lv_obj_t *name_lbl = lv_label_create(block);
    lv_label_set_text(name_lbl, pm_port->name);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name_lbl, cw, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name_lbl, cx, cy);
    lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_CLICKABLE);

    int indicator_top  = cy + 14 + 3;
    int indicator_avail_h = ch - 14 - 3;

    if (pm_port->toggled) {
        /* ── Toggle dot (visual) ── */
        bool active = value > 0.5f;
        int dot_d = indicator_avail_h > 20 ? 20 : indicator_avail_h;
        lv_obj_t *dot = lv_obj_create(block);
        lv_obj_set_size(dot, dot_d, dot_d);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot,
            active ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_shadow_color(dot, active ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
        lv_obj_set_style_shadow_width(dot, active ? 8 : 0, 0);
        lv_obj_set_style_shadow_spread(dot, 2, 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_60, 0);
        lv_obj_set_pos(dot,
            cx + (cw - dot_d) / 2,
            indicator_top + (indicator_avail_h - dot_d) / 2);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        if (cc) {
            cc->indicator = dot;
            /* Transparent hit zone — tap toggles, long-press opens editor */
            lv_obj_t *btn = lv_obj_create(block);
            lv_obj_set_size(btn, cw, ch);
            lv_obj_set_pos(btn, cx, cy);
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(btn, cell_toggle_cb,     LV_EVENT_CLICKED,      cc);
            lv_obj_add_event_cb(btn, cell_long_press_cb, LV_EVENT_LONG_PRESSED, cc);
        }

    } else if (pm_port->enumeration && pm_port->enum_count > 0) {
        /* ── Enum label (visual) ── */
        const char *enum_str = "?";
        for (int k = 0; k < pm_port->enum_count; k++) {
            if (fabsf(pm_port->enum_values[k] - value) < 0.5f) {
                enum_str = pm_port->enum_labels[k];
                break;
            }
        }
        lv_obj_t *val_lbl = lv_label_create(block);
        lv_label_set_text(val_lbl, enum_str);
        lv_label_set_long_mode(val_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_size(val_lbl, cw, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(val_lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(val_lbl, cx, indicator_top + (indicator_avail_h - 14) / 2);
        lv_obj_clear_flag(val_lbl, LV_OBJ_FLAG_CLICKABLE);
        if (cc) {
            cc->indicator = val_lbl;
            /* Transparent hit zone — tap cycles, long-press opens editor */
            lv_obj_t *btn = lv_obj_create(block);
            lv_obj_set_size(btn, cw, ch);
            lv_obj_set_pos(btn, cx, cy);
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(btn, cell_enum_cb,       LV_EVENT_CLICKED,      cc);
            lv_obj_add_event_cb(btn, cell_long_press_cb, LV_EVENT_LONG_PRESSED, cc);
        }

    } else {
        /* ── Arc knob (interactive) ── */
        int arc_d = indicator_avail_h - 4;
        if (arc_d > ARC_MAX_D) arc_d = ARC_MAX_D;
        if (arc_d < ARC_MIN_D) arc_d = ARC_MIN_D;

        lv_obj_t *arc = lv_arc_create(block);
        lv_obj_set_size(arc, arc_d, arc_d);
        lv_obj_set_pos(arc,
            cx + (cw - arc_d) / 2,
            indicator_top + (indicator_avail_h - arc_d) / 2);

        lv_arc_set_bg_angles(arc, 135, 45);
        lv_arc_set_range(arc, 0, 1000);

        int val_norm = 0;
        if (pm_port->max > pm_port->min)
            val_norm = (int)(1000.0f * (value - pm_port->min) /
                             (pm_port->max - pm_port->min));
        if (val_norm < 0)    val_norm = 0;
        if (val_norm > 1000) val_norm = 1000;
        lv_arc_set_value(arc, val_norm);

        lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A2A4A), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
        /* Small visible knob so user sees it's draggable */
        lv_obj_set_style_bg_color(arc, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_60, LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, -4, LV_PART_KNOB);
        lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_radius(arc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLL_CHAIN);
        if (cc) {
            /* Arc is interactive — register VALUE_CHANGED only.
             * No LONG_PRESSED: holding before dragging would open the editor. */
            lv_obj_add_event_cb(arc, cell_arc_cb, LV_EVENT_VALUE_CHANGED, cc);
            cc->indicator = arc;
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────────────── */

lv_obj_t *ui_plugin_block_create(lv_obj_t *parent, pb_plugin_t *plug,
                                  block_cb_t on_tap,
                                  block_cb_t on_bypass,
                                  block_cb_t on_remove,
                                  block_param_cb_t on_param,
                                  void *userdata)
{
    const pm_plugin_info_t *pi_info = pm_plugin_by_uri(plug->uri);
    lv_color_t accent = ui_plugin_block_category_color(pi_info ? pi_info->category : "");

    /* Check for user-customized control selection */
    char custom_syms[WIDGET_PREFS_MAX_CTRL][PB_SYMBOL_MAX];
    int  custom_count = widget_prefs_get(plug->symbol, custom_syms);
    bool use_custom = (custom_count > 0);

    /* Block dimensions */
    int n_displayed = use_custom ? custom_count
        : (pi_info
            ? ((pi_info->modgui_port_count > 0)
                   ? pi_info->modgui_port_count
                   : pi_info->ctrl_in_count)
            : 0);
    int block_w = (n_displayed > 4) ? BLOCK_W_2X : BLOCK_W_1X;
    int block_h = BLOCK_H;

    /* Control grid geometry — up to 4 cols for 2× blocks, 2 cols for 1× */
    int n_cols  = (n_displayed > 4) ? 4 : (n_displayed > 0 ? LV_MIN(n_displayed, 2) : 1);
    int n_rows  = (n_cols > 0) ? (n_displayed + n_cols - 1) / n_cols : 0;
    if (n_rows > 2) n_rows = 2;
    int max_ctrl = n_cols * n_rows;

    int avail_w = block_w - 2 * BLOCK_PAD;
    int avail_h = block_h - 2 * BLOCK_PAD - NAME_H - NAME_GAP;
    int cell_w  = (n_cols > 1)
        ? (avail_w - (n_cols - 1) * CELL_GAP) / n_cols
        : avail_w;
    int cell_h  = (n_rows > 1)
        ? (avail_h - (n_rows - 1) * CELL_GAP) / n_rows
        : avail_h;

    /* ── Create block container ── */
    block_ctx_t *ctx = malloc(sizeof(*ctx));
    ctx->on_tap    = on_tap;
    ctx->on_bypass = on_bypass;
    ctx->on_remove = on_remove;
    ctx->on_param  = on_param;
    ctx->userdata  = userdata;
    ctx->bypassed  = !plug->enabled;
    ctx->accent    = accent;
    ctx->cell_count = 0;

    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, block_w, block_h);
    lv_obj_set_user_data(block, ctx);

    /* Category-tinted background with angled gradient */
    lv_color_t bg = plug->enabled
        ? lv_color_mix(accent, lv_color_hex(0x0D0D1E), 55)
        : lv_color_hex(0x252535);
    apply_block_bg(block, bg);

    lv_obj_set_style_border_side(block, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(block, 3, 0);
    lv_obj_set_style_border_color(block,
        plug->enabled ? accent : UI_COLOR_BYPASS, 0);
    lv_obj_set_style_radius(block, 10, 0);

    lv_obj_set_style_shadow_color(block, accent, 0);
    lv_obj_set_style_shadow_width(block, plug->enabled ? 18 : 0, 0);
    lv_obj_set_style_shadow_spread(block, 2, 0);
    lv_obj_set_style_shadow_opa(block, 50, 0);
    lv_obj_set_style_shadow_ofs_x(block, 0, 0);
    lv_obj_set_style_shadow_ofs_y(block, 0, 0);

    lv_obj_set_style_pad_all(block, 0, 0);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLL_CHAIN); /* prevent scroll from chaining to canvas on touch */
    lv_obj_add_flag(block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(block, block_delete_cb, LV_EVENT_DELETE, NULL);

    /* ── Plugin name ── */
    const char *display_name = (pi_info && pi_info->name[0])
        ? pi_info->name : plug->label;
    lv_obj_t *name_lbl = lv_label_create(block);
    lv_label_set_text(name_lbl, display_name);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name_lbl, block_w - 2 * BLOCK_PAD, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(name_lbl, BLOCK_PAD, BLOCK_PAD);
    lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_CLICKABLE);

    /* ── Control cells ── */
    int grid_x0 = BLOCK_PAD;
    int grid_y0 = BLOCK_PAD + NAME_H + NAME_GAP;
    int ctrl_drawn = 0;

    if (pi_info && n_displayed > 0) {
        bool use_modgui = (!use_custom && pi_info->modgui_port_count > 0);
        int  src_count  = use_custom  ? custom_count
                        : use_modgui  ? pi_info->modgui_port_count
                        :               pi_info->port_count;

        for (int k = 0; k < src_count && ctrl_drawn < max_ctrl; k++) {
            /* Resolve pm_port_info_t for this cell */
            const pm_port_info_t *pm_port = NULL;
            if (use_custom) {
                /* Find port by custom symbol */
                for (int j = 0; j < pi_info->port_count; j++) {
                    if (pi_info->ports[j].type == PM_PORT_CONTROL_IN &&
                        strcmp(pi_info->ports[j].symbol, custom_syms[k]) == 0)
                    {
                        pm_port = &pi_info->ports[j];
                        break;
                    }
                }
                if (!pm_port) continue; /* symbol not found — skip */
            } else if (use_modgui) {
                const char *sym = pi_info->modgui_ports[k].symbol;
                for (int j = 0; j < pi_info->port_count; j++) {
                    if (pi_info->ports[j].type == PM_PORT_CONTROL_IN &&
                        strcmp(pi_info->ports[j].symbol, sym) == 0)
                    {
                        pm_port = &pi_info->ports[j];
                        break;
                    }
                }
                if (!pm_port) continue;
            } else {
                if (pi_info->ports[k].type != PM_PORT_CONTROL_IN) continue;
                pm_port = &pi_info->ports[k];
            }

            float val = port_value(plug, pm_port);
            int col = ctrl_drawn % n_cols;
            int row = ctrl_drawn / n_cols;
            int cx  = grid_x0 + col * (cell_w + CELL_GAP);
            int cy  = grid_y0 + row * (cell_h + CELL_GAP);

            cell_ctx_t *cc = NULL;
            if (ctx->cell_count < BLOCK_MAX_CELLS) {
                cc = &ctx->cells[ctx->cell_count++];
                memset(cc, 0, sizeof(*cc));
                cc->bc = ctx;
            }
            create_ctrl_cell(block, cx, cy, cell_w, cell_h, pm_port, val, accent, cc);
            ctrl_drawn++;
        }
    }

    /* ── "More controls" indicator ── */
    if (pi_info && n_displayed > max_ctrl) {
        char more_str[12];
        snprintf(more_str, sizeof(more_str), "+%d", n_displayed - max_ctrl);
        lv_obj_t *more_lbl = lv_label_create(block);
        lv_label_set_text(more_lbl, more_str);
        lv_obj_set_style_text_font(more_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(more_lbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_align(more_lbl, LV_ALIGN_BOTTOM_RIGHT, -BLOCK_PAD, -BLOCK_PAD);
        lv_obj_clear_flag(more_lbl, LV_OBJ_FLAG_CLICKABLE);
    }

    /* ── No-controls fallback: show category dot ── */
    if (n_displayed == 0) {
        lv_obj_t *dot = lv_obj_create(block);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, plug->enabled ? accent : UI_COLOR_BYPASS, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_RIGHT, -BLOCK_PAD, -BLOCK_PAD);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    /* Long press → parameter editor; short press freed for widget interaction */
    lv_obj_add_event_cb(block, block_open_editor_cb, LV_EVENT_LONG_PRESSED, ctx);

    return block;
}

void ui_plugin_block_set_bypassed(lv_obj_t *block, bool bypassed)
{
    block_ctx_t *ctx = lv_obj_get_user_data(block);
    lv_color_t accent = ctx ? ctx->accent : UI_COLOR_PRIMARY;

    lv_color_t bg = bypassed
        ? lv_color_hex(0x252535)
        : lv_color_mix(accent, lv_color_hex(0x0D0D1E), 55);
    apply_block_bg(block, bg);
    lv_obj_set_style_border_color(block, bypassed ? UI_COLOR_BYPASS : accent, 0);
    lv_obj_set_style_shadow_width(block, bypassed ? 0 : 18, 0);
    lv_obj_set_style_shadow_color(block, accent, 0);
    lv_obj_set_style_shadow_spread(block, 2, 0);
    lv_obj_set_style_shadow_opa(block, 50, 0);
}

void ui_plugin_block_set_param(lv_obj_t *block, const char *symbol, float value)
{
    block_ctx_t *ctx = lv_obj_get_user_data(block);
    if (!ctx) return;
    for (int i = 0; i < ctx->cell_count; i++) {
        cell_ctx_t *cc = &ctx->cells[i];
        if (strcmp(cc->symbol, symbol) != 0) continue;
        cc->value = value;
        if (!cc->indicator) break;
        if (cc->is_toggle) {
            bool active = value > 0.5f;
            lv_obj_set_style_bg_color(cc->indicator,
                active ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
            lv_obj_set_style_shadow_color(cc->indicator,
                active ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
            lv_obj_set_style_shadow_width(cc->indicator, active ? 8 : 0, 0);
        } else if (cc->is_enum) {
            for (int k = 0; k < cc->enum_count; k++) {
                if (fabsf(cc->enum_values[k] - value) < 0.5f) {
                    lv_label_set_text(cc->indicator, cc->enum_labels[k]);
                    break;
                }
            }
        } else {
            /* arc — guard with updating flag to suppress VALUE_CHANGED callback */
            int val_norm = 0;
            if (cc->max > cc->min)
                val_norm = (int)(1000.0f * (value - cc->min) / (cc->max - cc->min));
            if (val_norm < 0)    val_norm = 0;
            if (val_norm > 1000) val_norm = 1000;
            cc->updating = true;
            lv_arc_set_value(cc->indicator, val_norm);
            cc->updating = false;
        }
        break;
    }
}
