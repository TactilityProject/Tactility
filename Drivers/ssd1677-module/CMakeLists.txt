cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

tactility_add_module(ssd1677-module
    SRCS source/ssd1677.cpp source/module.cpp
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel platform-esp32 driver esp_timer
)

# The pixel loops are too slow with the debug optimization level of the project
set_source_files_properties(source/ssd1677.cpp PROPERTIES COMPILE_OPTIONS -O2)
