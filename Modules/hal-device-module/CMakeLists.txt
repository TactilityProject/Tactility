cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

list(APPEND REQUIRES_LIST
    TactilityKernel
    TactilityCore
    TactilityFreeRtos
)

if (NOT DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST freertos_kernel)
endif ()

tactility_add_module(hal-device-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)
