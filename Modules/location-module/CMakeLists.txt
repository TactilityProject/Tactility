cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

list(APPEND REQUIRES_LIST
    TactilityKernel
    service-module
    gps-module
    http-module
    cjson-module
    minmea
)

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST lwip)
endif ()

tactility_add_module(location-module
    SRCS ${SOURCE_FILES}
    PRIV_INCLUDE_DIRS private/
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)
