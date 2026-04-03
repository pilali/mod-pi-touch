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

# ─── Source files ─────────────────────────────────────────────────────────────
set(SOURCES
    src/main.c
    src/host_comm.c
    src/pedalboard.c
    src/snapshot.c
    src/plugin_manager.c
    src/bank.c
    src/settings.c
    src/lv2_utils.c
    src/ui/ui_app.c
    src/ui/ui_pedalboard.c
    src/ui/ui_plugin_block.c
    src/ui/ui_plugin_browser.c
    src/ui/ui_param_editor.c
    src/ui/ui_snapshot_bar.c
    src/ui/ui_bank_browser.c
    src/ui/ui_settings.c
    ${CJSON_SRC}
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
)

target_link_libraries(mod-pi-touch PRIVATE
    lvgl
    ${LILV_LIBRARIES}
    ${SERD_LIBRARIES}
    ${SORD_LIBRARIES}
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
