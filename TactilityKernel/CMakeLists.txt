cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "Source/*.c**")

list(APPEND REQUIRES_LIST)

if (DEFINED ENV{ESP_IDF_VERSION})
    # TODO move the related logic for esp_time in Tactility/time.h into the Platform/ subproject
    list(APPEND REQUIRES_LIST esp_timer)
else ()
    list(APPEND REQUIRES_LIST freertos_kernel)
endif ()

tactility_add_module(TactilityKernel
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS Include/
    REQUIRES ${REQUIRES_LIST}
)
