#include "ui_param_editor.h"
#include "ui_app.h"
#include "../host_comm.h"

#include <string.h>
#include <stdio.h>

/* ─── Editor state ───────────────────────────────────────────────────────────── */
static lv_obj_t           *g_modal       = NULL;
static int                 g_instance    = -1;
static bool                g_enabled     = true;
static bypass_toggle_cb_t  g_bypass_cb   = NULL;
static void               *g_bypass_ud   = NULL;
static lv_obj_t           *g_bypass_lbl  = NULL; /* label in bypass toggle btn */
static param_change_cb_t   g_value_cb    = NULL;
static void               *g_value_ud    = NULL;

/* Array of (symbol, arc_obj) for live update */
#define MAX_ARCS 64
static struct { char symbol[PB_SYMBOL_MAX]; lv_obj_t *arc; lv_obj_t *val_lbl; } g_arcs[MAX_ARCS];
static int g_arc_count = 0;

/* ─── Bypass toggle callback ─────────────────────────────────────────────────── */

static void bypass_btn_cb(lv_event_t *e)
{
    (void)e;
    g_enabled = !g_enabled;
    lv_label_set_text(g_bypass_lbl, g_enabled ? "Enabled" : "Disabled");
    lv_obj_t *btn = lv_obj_get_parent(g_bypass_lbl);
    lv_obj_set_style_bg_color(btn, g_enabled ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
    if (g_bypass_cb) g_bypass_cb(g_bypass_ud);
}

/* ─── Arc change callback ────────────────────────────────────────────────────── */
typedef struct {
    char   symbol[PB_SYMBOL_MAX];
    float  min;
    float  max;
    lv_obj_t *val_lbl;
} arc_ctx_t;

static void arc_value_cb(lv_event_t *e)
{
    arc_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *arc  = lv_event_get_target(e);
    int arc_val = lv_arc_get_value(arc);

    /* Map arc 0-100 → param min..max */
    float ratio = arc_val / 100.0f;
    float value = ctx->min + ratio * (ctx->max - ctx->min);

    /* Update label */
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3g", (double)value);
    lv_label_set_text(ctx->val_lbl, buf);

    /* Send to mod-host */
    if (g_instance >= 0)
        host_param_set(g_instance, ctx->symbol, value);

    /* User callback */
    if (g_value_cb) g_value_cb(g_instance, ctx->symbol, value, g_value_ud);
}

/* ─── Close ──────────────────────────────────────────────────────────────────── */
static void close_cb(lv_event_t *e)
{
    (void)e;
    ui_param_editor_close();
}

/* ─── Public API ─────────────────────────────────────────────────────────────── */

void ui_param_editor_show(int instance_id, const char *plugin_label,
                          pb_port_t *ports, int port_count,
                          bool enabled,
                          bypass_toggle_cb_t bypass_cb, void *bypass_ud,
                          param_change_cb_t value_cb, void *userdata)
{
    if (g_modal) ui_param_editor_close();

    g_instance   = instance_id;
    g_enabled    = enabled;
    g_bypass_cb  = bypass_cb;
    g_bypass_ud  = bypass_ud;
    g_bypass_lbl = NULL;
    g_value_cb   = value_cb;
    g_value_ud   = userdata;
    g_arc_count  = 0;

    /* Modal overlay */
    g_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_70, 0);

    /* Panel */
    lv_obj_t *panel = lv_obj_create(g_modal);
    lv_obj_set_size(panel, 900, 540);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 8, 0);

    /* Title bar */
    lv_obj_t *title_row = lv_obj_create(panel);
    lv_obj_set_size(title_row, LV_PCT(100), 44);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);

    lv_obj_t *title_lbl = lv_label_create(title_row);
    lv_label_set_text(title_lbl, plugin_label);
    lv_obj_set_style_text_color(title_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);

    lv_obj_t *btn_close = lv_btn_create(title_row);
    lv_obj_set_size(btn_close, 36, 36);
    lv_obj_set_style_bg_color(btn_close, UI_COLOR_BYPASS, 0);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_close);
    lv_obj_add_event_cb(btn_close, close_cb, LV_EVENT_CLICKED, NULL);

    /* Params grid (scrollable) */
    lv_obj_t *scroll = lv_obj_create(panel);
    lv_obj_set_size(scroll, LV_PCT(100), 0); /* height managed by flex_grow */
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(scroll, 16, 0); lv_obj_set_style_pad_column(scroll, 16, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);

    /* Bypass toggle button — always first */
    {
        lv_obj_t *card = lv_obj_create(scroll);
        lv_obj_set_size(card, 130, 160);
        lv_obj_set_style_bg_color(card, UI_COLOR_BG, 0);
        lv_obj_set_style_border_color(card, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 8, 0);

        lv_obj_t *name_lbl = lv_label_create(card);
        lv_label_set_text(name_lbl, "bypass");
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name_lbl, LV_PCT(100));
        lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);

        lv_obj_t *btn = lv_btn_create(card);
        lv_obj_set_size(btn, 90, 44);
        lv_obj_set_style_bg_color(btn,
            g_enabled ? UI_COLOR_ACTIVE : UI_COLOR_BYPASS, 0);
        g_bypass_lbl = lv_label_create(btn);
        lv_label_set_text(g_bypass_lbl, g_enabled ? "Enabled" : "Disabled");
        lv_obj_center(g_bypass_lbl);
        lv_obj_set_style_text_font(g_bypass_lbl, &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(btn, bypass_btn_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Create a control widget for each control input port */
    for (int i = 0; i < port_count; i++) {
        pb_port_t *port = &ports[i];
        if (!port->snapshotable && port->symbol[0] == ':') continue;

        /* Card per parameter */
        lv_obj_t *card = lv_obj_create(scroll);
        lv_obj_set_size(card, 130, 160);
        lv_obj_set_style_bg_color(card, UI_COLOR_BG, 0);
        lv_obj_set_style_border_color(card, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 4, 0); lv_obj_set_style_pad_column(card, 4, 0);

        /* Port name label */
        lv_obj_t *name_lbl = lv_label_create(card);
        lv_label_set_text(name_lbl, port->symbol);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name_lbl, LV_PCT(100));
        lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);

        /* Arc knob */
        lv_obj_t *arc = lv_arc_create(card);
        lv_obj_set_size(arc, 90, 90);
        lv_arc_set_range(arc, 0, 100);

        /* Map current value to 0-100 */
        float range = port->max - port->min;
        int arc_val = (range > 0.0f) ?
            (int)(((port->value - port->min) / range) * 100.0f) : 0;
        lv_arc_set_value(arc, arc_val);
        lv_obj_set_style_arc_color(arc, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
        lv_obj_center(arc);

        /* Value label */
        char val_str[32];
        snprintf(val_str, sizeof(val_str), "%.3g", (double)port->value);
        lv_obj_t *val_lbl = lv_label_create(card);
        lv_label_set_text(val_lbl, val_str);
        lv_obj_set_style_text_color(val_lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, 0);

        /* Arc context */
        arc_ctx_t *ctx = malloc(sizeof(*ctx));
        snprintf(ctx->symbol, sizeof(ctx->symbol), "%s", port->symbol);
        ctx->min     = port->min;
        ctx->max     = port->max;
        ctx->val_lbl = val_lbl;
        lv_obj_add_event_cb(arc, arc_value_cb, LV_EVENT_VALUE_CHANGED, ctx);

        /* Register for live update */
        if (g_arc_count < MAX_ARCS) {
            snprintf(g_arcs[g_arc_count].symbol, PB_SYMBOL_MAX, "%s", port->symbol);
            g_arcs[g_arc_count].arc     = arc;
            g_arcs[g_arc_count].val_lbl = val_lbl;
            g_arc_count++;
        }
    }
}

void ui_param_editor_close(void)
{
    if (g_modal) {
        lv_obj_del(g_modal);
        g_modal     = NULL;
        g_instance  = -1;
        g_arc_count = 0;
    }
}

void ui_param_editor_update(const char *symbol, float value)
{
    for (int i = 0; i < g_arc_count; i++) {
        if (strcmp(g_arcs[i].symbol, symbol) == 0) {
            /* Update arc (would need to know min/max — stored in ctx; skip for now) */
            char buf[32];
            snprintf(buf, sizeof(buf), "%.3g", (double)value);
            lv_label_set_text(g_arcs[i].val_lbl, buf);
            break;
        }
    }
}
