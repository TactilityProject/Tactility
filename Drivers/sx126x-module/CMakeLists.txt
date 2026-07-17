cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*" "private/*.c*")

tactility_add_module(sx126x-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    PRIV_INCLUDE_DIRS private/
    REQUIRES TactilityKernel platform-esp32 radiolib driver esp_timer
)
