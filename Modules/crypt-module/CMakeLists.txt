cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

list(APPEND REQUIRES_LIST
    TactilityKernel
    mbedtls
)

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST
        nvs_flash
        esp_hw_support
        esp_rom
    )
endif ()

tactility_add_module(crypt-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)
