cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../Buildscripts/module.cmake")

list(APPEND REQUIRES_LIST
    lvgl
)

list(APPEND PRIV_REQUIRES_LIST
    Tactility
    TactilityKernel
    mbedtls
)

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND PRIV_REQUIRES_LIST elf_loader)
endif ()

if (IDF_TARGET STREQUAL "esp32p4")
    list(APPEND PRIV_REQUIRES_LIST esp_driver_ppa esp_mm)
endif ()

file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

tactility_add_module(TactilityC
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS Include/
    PRIV_INCLUDE_DIRS Private/
    REQUIRES ${REQUIRES_LIST}
    PRIV_REQUIRES ${PRIV_REQUIRES_LIST}
)
