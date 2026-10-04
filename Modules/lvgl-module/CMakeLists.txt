cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/properties.cmake")

# ####################################
# Read device properties
# ####################################

# Load device.properties as a map
get_property(TACTILITY_DEVICE_ID GLOBAL PROPERTY TACTILITY_DEVICE_ID)
READ_PROPERTIES_TO_MAP(
    "${CMAKE_CURRENT_LIST_DIR}/../../Devices/${TACTILITY_DEVICE_ID}/device.properties"
    device_properties
)
# Read UI density
GET_VALUE_FROM_MAP(device_properties "lvgl.uiDensity" ui_density)
# Define UiDensity enum value
if (ui_density)
    if (ui_density STREQUAL "default")
        set(ui_density_variable "LVGL_UI_DENSITY_DEFAULT")
    elseif (ui_density STREQUAL "compact")
        set(ui_density_variable "LVGL_UI_DENSITY_COMPACT")
    else ()
        message(FATAL_ERROR "Invalid lvgl.uiDensity: '${ui_density}'. Must be either 'default' or 'compact'")
    endif ()
    message("UI density set to '${ui_density}' via properties")
else ()
    set(ui_density "default")
    set(ui_density_variable "LVGL_UI_DENSITY_DEFAULT")
    message("UI density set to default: ${ui_density}")
endif ()

# ####################################
# Create module
# ####################################

# Regular source files
file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

list(APPEND REQUIRES_LIST
    TactilityKernel
    binfont-module
    lodepng-module
    lvgl
)

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST esp_lvgl_port)
    # required for lvgl_ppa
    if (IDF_TARGET STREQUAL "esp32p4")
        list(APPEND REQUIRES_LIST esp_driver_ppa)
    endif ()
else ()
    list(APPEND REQUIRES_LIST freertos_kernel)
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

tactility_add_module(lvgl-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    PRIV_INCLUDE_DIRS private/
    REQUIRES ${REQUIRES_LIST}
    # lv_mem_custom.c provides lv_mem_init/lv_malloc_core/etc, which lvgl__lvgl's own lv_init.c/
    # lv_mem.c call back into (LV_STDLIB_CUSTOM) - a reverse reference into this module that a
    # normal single-pass static link can't resolve. See module.cmake's WHOLE_ARCHIVE comment.
    WHOLE_ARCHIVE
)

tactility_get_module_name("lvgl-module" MODULE_NAME)

target_compile_definitions(${MODULE_NAME} PUBLIC
    # Ensure it loads <lvgl.h>
    "-DLV_LVGL_H_INCLUDE_SIMPLE"
    # UiDensity
    "-DTT_LVGL_UI_DENSITY=${ui_density_variable}"
)
