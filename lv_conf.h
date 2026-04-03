/**
 * lv_conf.h — LVGL configuration for mod-pi-touch
 * Target: Raspberry Pi, 7" touch display, 1280×720, framebuffer
 */

#if 1 /* Set this to 1 to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ─── Color depth ──────────────────────────────────────────────────────────── */
#define LV_COLOR_DEPTH 32

/* ─── Memory ────────────────────────────────────────────────────────────────── */
#define LV_MEM_SIZE (8 * 1024 * 1024U)  /* 8 MB for UI objects */

/* ─── HAL settings ─────────────────────────────────────────────────────────── */
#define LV_DEF_REFR_PERIOD 16   /* ~60 fps */
#define LV_INDEV_DEF_READ_PERIOD 16

/* ─── Display resolution ───────────────────────────────────────────────────── */
#define LV_HOR_RES_MAX 1280
#define LV_VER_RES_MAX 720

/* ─── Linux framebuffer driver ─────────────────────────────────────────────── */
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BSD 0

/* ─── Evdev input driver ────────────────────────────────────────────────────── */
#define LV_USE_EVDEV 1

/* ─── Logging ───────────────────────────────────────────────────────────────── */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_LOG_TRACE_MEM 0
#define LV_LOG_TRACE_TIMER 0
#define LV_LOG_TRACE_INDEV 0
#define LV_LOG_TRACE_DISP_REFR 0
#define LV_LOG_TRACE_EVENT 0
#define LV_LOG_TRACE_OBJ_CREATE 0
#define LV_LOG_TRACE_LAYOUT 0
#define LV_LOG_TRACE_ANIM 0

/* ─── Fonts ─────────────────────────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ─── Widgets ───────────────────────────────────────────────────────────────── */
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_SLIDER 1
#define LV_USE_ARC 1
#define LV_USE_LIST 1
#define LV_USE_DROPDOWN 1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_MSGBOX 1
#define LV_USE_TABVIEW 1
#define LV_USE_CANVAS 1
#define LV_USE_LINE 1
#define LV_USE_IMAGE 1
#define LV_USE_SWITCH 1
#define LV_USE_SPINNER 1
#define LV_USE_BAR 1

/* ─── Animations ────────────────────────────────────────────────────────────── */
#define LV_USE_ANIM 1

/* ─── Layouts ───────────────────────────────────────────────────────────────── */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* ─── Drawing ───────────────────────────────────────────────────────────────── */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1

/* ─── Perf monitor (debug) ──────────────────────────────────────────────────── */
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/* ─── File system (not used) ────────────────────────────────────────────────── */
#define LV_USE_FS_POSIX 0

/* ─── Assert ────────────────────────────────────────────────────────────────── */
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/* ─── Garbage collection ────────────────────────────────────────────────────── */
#define LV_ENABLE_GC 0

#endif /* LV_CONF_H */
#endif /* Enable content */
