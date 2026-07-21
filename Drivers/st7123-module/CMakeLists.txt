cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(st7123-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel platform-esp32 esp_lcd_st7123 esp_lcd_touch_st7123 esp_lcd driver esp_hw_support
)
