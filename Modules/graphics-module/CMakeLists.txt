cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

list(APPEND REQUIRES_LIST
    TactilityKernel
)

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST
        esp_mm # esp_cache.h, used by pixel_buffer_msync()
    )
endif ()

tactility_add_module(graphics-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)
