cmake_minimum_required(VERSION 3.20)

# ####################################
# Read sdkconfig properties
# ####################################

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/properties.cmake")

if (DEFINED ENV{ESP_IDF_VERSION})
    GET_PROPERTY_FILE_CONTENT("${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig" sdkconfig_text)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_LVGL_FONT_SIZE_SMALL" font_size_small)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_LVGL_FONT_SIZE_DEFAULT" font_size_default)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_LVGL_FONT_SIZE_LARGE" font_size_large)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE" statusbar_symbol_size)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE" launcher_symbol_size)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_LVGL_SHARED_ICON_SIZE" shared_symbol_size)
else ()
    # Default values for simulator
    set(font_size_small 10)
    set(font_size_default 14)
    set(font_size_large 18)
    set(statusbar_symbol_size 20)
    set(launcher_symbol_size 36)
    set(shared_symbol_size 16)
endif ()

message("Font sizes:")
message(" - small: ${font_size_small}")
message(" - default: ${font_size_default}")
message(" - large:  ${font_size_large}")
message("Icon sizes:")
message(" - statusbar: ${statusbar_symbol_size}")
message(" - launcher: ${launcher_symbol_size}")
message(" - shared: ${shared_symbol_size}")

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
GET_VALUE_FROM_MAP(device_properties "[lvgl]uiDensity" ui_density)
# Define UiDensity enum value
if (ui_density)
    if (ui_density STREQUAL "default")
        set(ui_density_variable "LVGL_UI_DENSITY_DEFAULT")
    elseif (ui_density STREQUAL "compact")
        set(ui_density_variable "LVGL_UI_DENSITY_COMPACT")
    else ()
        message(FATAL_ERROR "Invalid [lvgl]uiDensity: '${ui_density}'. Must be either 'default' or 'compact'")
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
# Font source files
list(APPEND SOURCE_FILES "source-fonts/material_symbols_statusbar_${statusbar_symbol_size}.c")
list(APPEND SOURCE_FILES "source-fonts/material_symbols_launcher_${launcher_symbol_size}.c")
list(APPEND SOURCE_FILES "source-fonts/material_symbols_shared_${shared_symbol_size}.c")

list(APPEND REQUIRES_LIST
    TactilityKernel
    lvgl
)

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST esp_lvgl_port)
else ()
    list(APPEND REQUIRES_LIST freertos_kernel)
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

tactility_add_module(lvgl-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)

tactility_get_module_name("lvgl-module" MODULE_NAME)

target_compile_definitions(${MODULE_NAME} PUBLIC
    # Ensure it loads <lvgl.h>
    "-DLV_LVGL_H_INCLUDE_SIMPLE"
    # Text fonts
    "-DTT_LVGL_TEXT_FONT_SMALL_SIZE=${font_size_small}"
    "-DTT_LVGL_TEXT_FONT_SMALL_SYMBOL=lv_font_montserrat_${font_size_small}"
    "-DTT_LVGL_TEXT_FONT_DEFAULT_SIZE=${font_size_default}"
    "-DTT_LVGL_TEXT_FONT_DEFAULT_SYMBOL=lv_font_montserrat_${font_size_default}"
    "-DTT_LVGL_TEXT_FONT_LARGE_SIZE=${font_size_large}"
    "-DTT_LVGL_TEXT_FONT_LARGE_SYMBOL=lv_font_montserrat_${font_size_large}"
    # Icon fonts
    "-DTT_LVGL_STATUSBAR_FONT_ICON_SIZE=${statusbar_symbol_size}"
    "-DTT_LVGL_STATUSBAR_FONT_ICON_SYMBOL=material_symbols_statusbar_${statusbar_symbol_size}"
    "-DTT_LVGL_LAUNCHER_FONT_ICON_SIZE=${launcher_symbol_size}"
    "-DTT_LVGL_LAUNCHER_FONT_ICON_SYMBOL=material_symbols_launcher_${launcher_symbol_size}"
    "-DTT_LVGL_SHARED_FONT_ICON_SIZE=${shared_symbol_size}"
    "-DTT_LVGL_SHARED_FONT_ICON_SYMBOL=material_symbols_shared_${shared_symbol_size}"
    # UiDensity
    "-DTT_LVGL_UI_DENSITY=${ui_density_variable}"
)
