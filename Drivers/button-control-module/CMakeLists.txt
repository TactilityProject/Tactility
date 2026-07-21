cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(button-control-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel platform-esp32 lvgl driver
)
