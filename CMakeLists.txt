cmake_minimum_required(VERSION 3.18)
project(mod-pi-touch C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2")

# ─── Options ─────────────────────────────────────────────────────────────────
option(USE_FBDEV  "Use Linux framebuffer display driver" ON)
option(USE_EVDEV  "Use evdev touch/mouse input driver"   ON)

# ─── Find system packages ────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(LILV   REQUIRED lilv-0)
pkg_check_modules(SERD   REQUIRED serd-0)
pkg_check_modules(SORD   REQUIRED sord-0)

# Use the system JACK (1.9.21) — same version mod-host links against.
# The custom JACK in /usr/local is 1.9.14 and fails jack_client_open (0x11).
find_library(JACK_SYS_LIB jack
    PATHS /lib/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu
    NO_DEFAULT_PATH)
if(JACK_SYS_LIB)
    set(JACK_LIBRARIES    ${JACK_SYS_LIB})
    set(JACK_INCLUDE_DIRS /usr/local/include)   # compatible JACK headers
    set(JACK_CFLAGS_OTHER "")
    message(STATUS "Using system JACK: ${JACK_SYS_LIB}")
else()
    pkg_check_modules(JACK REQUIRED jack)
    message(STATUS "Using pkg-config JACK: ${JACK_LIBRARIES}")
endif()

# cJSON (bundled)
set(CJSON_SRC ${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c)
if(NOT EXISTS ${CJSON_SRC})
    message(FATAL_ERROR "cJSON not found. Run: git submodule update --init third_party/cjson")
endif()

# LVGL (bundled)
set(LVGL_DIR ${CMAKE_SOURCE_DIR}/third_party/lvgl)
if(NOT EXISTS ${LVGL_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "LVGL not found. Run: git submodule update --init third_party/lvgl")
endif()

# Pass lv_conf.h location to LVGL
set(LV_CONF_PATH ${CMAKE_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(${LVGL_DIR} lvgl EXCLUDE_FROM_ALL)

# Remove all SIMD assembly files — LVGL ships Helium (Cortex-M55) and NEON
# assembly stubs that cannot be assembled by GNU as on aarch64 because
# lv_conf_internal.h uses "#if 0 && defined(__ASSEMBLY__)" and C system headers
# leak into the assembler context causing "unknown mnemonic 'typedef'" errors.
# LVGL falls back to portable C blend paths when these are absent.
get_target_property(_lvgl_srcs lvgl SOURCES)
list(FILTER _lvgl_srcs EXCLUDE REGEX "\\.S$")
# Remove built-in Montserrat fonts — replaced by resources/fonts/ versions
# compiled with Latin-1 Supplement (0xC0-0xFF) for accented character support.
list(FILTER _lvgl_srcs EXCLUDE REGEX "lv_font_montserrat_[0-9]+\\.c$")
set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")

# ─── Source files ─────────────────────────────────────────────────────────────
set(SOURCES
    src/main.c
    src/i18n.c
    src/host_comm.c
    src/hw_detect.c
    src/pedalboard.c
    src/snapshot.c
    src/plugin_manager.c
    src/bank.c
    src/settings.c
    src/lv2_utils.c
    src/widget_prefs.c
    src/ui/ui_app.c
    src/ui/ui_pedalboard.c
    src/ui/ui_plugin_block.c
    src/ui/ui_plugin_browser.c
    src/ui/ui_param_editor.c
    src/ui/ui_file_browser.c
    src/ui/ui_snapshot_bar.c
    src/ui/ui_bank_browser.c
    src/ui/ui_settings.c
    src/ui/ui_conductor.c
    src/pre_fx.c
    src/tuner_host.c
    src/wifi_manager.c
    src/ui/ui_pre_fx.c
    src/ui/ui_scene.c
    src/ui/ui_splash.c
    ${CJSON_SRC}
    resources/fonts/lv_font_montserrat_12.c
    resources/fonts/lv_font_montserrat_14.c
    resources/fonts/lv_font_montserrat_16.c
    resources/fonts/lv_font_montserrat_18.c
    resources/fonts/lv_font_montserrat_20.c
    resources/fonts/lv_font_montserrat_24.c
    resources/fonts/lv_font_montserrat_28.c
    resources/fonts/lv_font_montserrat_32.c
)

# Linux display/input drivers from LVGL
if(USE_FBDEV)
    list(APPEND SOURCES ${LVGL_DIR}/src/drivers/display/fb/lv_linux_fbdev.c)
    add_compile_definitions(LV_USE_LINUX_FBDEV=1)
endif()

if(USE_EVDEV)
    list(APPEND SOURCES ${LVGL_DIR}/src/drivers/evdev/lv_evdev.c)
    add_compile_definitions(LV_USE_EVDEV=1)
endif()

# ─── Executable ───────────────────────────────────────────────────────────────
add_executable(mod-pi-touch ${SOURCES})

target_include_directories(mod-pi-touch PRIVATE
    src/
    third_party/cjson/
    ${LVGL_DIR}/
    ${LILV_INCLUDE_DIRS}
    ${SERD_INCLUDE_DIRS}
    ${SORD_INCLUDE_DIRS}
    ${JACK_INCLUDE_DIRS}
)

target_link_libraries(mod-pi-touch PRIVATE
    lvgl
    ${LILV_LIBRARIES}
    ${SERD_LIBRARIES}
    ${SORD_LIBRARIES}
    ${JACK_LIBRARIES}
    pthread
    m
)

target_compile_options(mod-pi-touch PRIVATE
    ${LILV_CFLAGS_OTHER}
    ${SERD_CFLAGS_OTHER}
    ${SORD_CFLAGS_OTHER}
    -Wall -Wextra -Wno-unused-parameter
)

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS mod-pi-touch DESTINATION bin)
