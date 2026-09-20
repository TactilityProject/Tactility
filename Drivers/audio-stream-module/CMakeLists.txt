cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

set(REQUIRES_LIST TactilityKernel)
if (DEFINED ENV{ESP_IDF_VERSION})
    # Decoder symbol exports (source/symbols.c) compile against the ESP-only audio codec component.
    list(APPEND REQUIRES_LIST espressif__esp_audio_codec)
endif ()

tactility_add_module(audio-stream-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)
