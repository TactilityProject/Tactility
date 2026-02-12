cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES "Source/*.c*")

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
    INCLUDE_DIRS Include/
    REQUIRES ${REQUIRES_LIST}
)

tactility_get_module_name("lvgl-module" MODULE_NAME)

target_compile_definitions(${MODULE_NAME} PUBLIC "-DLV_LVGL_H_INCLUDE_SIMPLE")
