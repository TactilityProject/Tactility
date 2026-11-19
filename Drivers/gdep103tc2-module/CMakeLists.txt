cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(gdep103tc2-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel esp_lcd esp_driver_gpio esp_timer esp_rom heap freertos
)

# The pixel loops of the display are too slow with current optimization level
set_source_files_properties(
    source/gdep103tc2_display.cpp
    source/epd/gdep103tc2_bus.cpp
    source/epd/gdep103tc2_frames.cpp
    source/epd/gdep103tc2_engine.cpp
    PROPERTIES COMPILE_OPTIONS -O2
)
