cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(lilygo-t5s3-epd-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel esp_lcd esp_driver_gpio esp_driver_rmt esp_timer esp_rom hal soc heap freertos
)

# The pixel loops of the display are too slow with the debug optimization level of the project
set_source_files_properties(
    source/t5s3_epd_board.cpp
    source/t5s3_display_bw.cpp
    source/t5s3_display.cpp
    PROPERTIES COMPILE_OPTIONS -O2
)
