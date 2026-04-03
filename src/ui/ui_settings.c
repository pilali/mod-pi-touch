#include "ui_settings.h"
#include "ui_app.h"
#include "../settings.h"
#include "../host_comm.h"

#include <string.h>
#include <stdio.h>

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_app_show_screen(UI_SCREEN_PEDALBOARD);
}

static void cpu_refresh_cb(lv_event_t *e)
{
    lv_obj_t *lbl = lv_event_get_user_data(e);
    float cpu = 0.0f;
    host_cpu_load(&cpu);
    char buf[64];
    snprintf(buf, sizeof(buf), "CPU: %.1f%%", (double)cpu);
    lv_label_set_text(lbl, buf);
}

static lv_obj_t *add_info_row(lv_obj_t *parent, const char *label, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);

    lv_obj_t *lbl_w = lv_label_create(row);
    lv_label_set_text(lbl_w, label);
    lv_obj_set_style_text_color(lbl_w, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *val_w = lv_label_create(row);
    lv_label_set_text(val_w, value);
    lv_obj_set_style_text_color(val_w, UI_COLOR_TEXT, 0);
    return val_w;
}

void ui_settings_show(lv_obj_t *parent)
{
    mpt_settings_t *s = settings_get();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_gap(parent, 12, 0);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 44);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_gap(hdr, 8, 0);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_set_style_bg_color(btn_back, UI_COLOR_PRIMARY, 0);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, "Settings");
    lv_obj_set_style_text_color(hdr_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_18, 0);

    /* Info rows */
    add_info_row(parent, "mod-host address", s->host_addr);

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", s->host_cmd_port);
    add_info_row(parent, "mod-host port", port_str);

    add_info_row(parent, "Pedalboards dir", s->pedalboards_dir);
    add_info_row(parent, "Framebuffer", s->fb_device);
    add_info_row(parent, "Touch device", s->touch_device);

    /* Connection status */
    add_info_row(parent, "mod-host status",
        host_comm_is_connected() ? "Connected" : "Disconnected");

    /* CPU load row with refresh button */
    lv_obj_t *cpu_row = lv_obj_create(parent);
    lv_obj_set_size(cpu_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cpu_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cpu_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cpu_row, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(cpu_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cpu_row, 0, 0);
    lv_obj_set_style_radius(cpu_row, 6, 0);
    lv_obj_set_style_pad_hor(cpu_row, 12, 0);
    lv_obj_set_style_pad_ver(cpu_row, 8, 0);

    lv_obj_t *cpu_key = lv_label_create(cpu_row);
    lv_label_set_text(cpu_key, "CPU load");
    lv_obj_set_style_text_color(cpu_key, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *cpu_val = lv_label_create(cpu_row);
    lv_label_set_text(cpu_val, "-- %");
    lv_obj_set_style_text_color(cpu_val, UI_COLOR_TEXT, 0);

    lv_obj_t *cpu_btn = lv_btn_create(cpu_row);
    lv_obj_set_size(cpu_btn, 44, 32);
    lv_obj_set_style_bg_color(cpu_btn, UI_COLOR_ACCENT, 0);
    lv_obj_t *lbl_refresh = lv_label_create(cpu_btn);
    lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH);
    lv_obj_center(lbl_refresh);
    lv_obj_add_event_cb(cpu_btn, cpu_refresh_cb, LV_EVENT_CLICKED, cpu_val);
}
