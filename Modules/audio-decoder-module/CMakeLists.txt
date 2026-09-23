cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

# include/tactility/esp_audio_simple_dec.h and esp_audio_types.h are vendored verbatim from
# espressif__esp_audio_codec so apps can include this module's header alone; symbols resolve
# from the firmware's module table at load, like any other kernel/platform export.
# ESP-only: there is no POSIX audio-decoder backend.
set(REQUIRES_LIST TactilityKernel)
if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST espressif__esp_audio_codec)
endif ()

tactility_add_module(audio-decoder-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)
