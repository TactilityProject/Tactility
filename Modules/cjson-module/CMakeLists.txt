cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

# include/cJSON.h is vendored verbatim from espressif__cjson so apps can `#include <cJSON.h>`
# against this module alone (no idf_component.yml entry of their own needed) - the app ELF keeps
# cJSON_* calls undefined and the firmware resolves them from this module's symbol table at load,
# same mechanism as any other kernel/platform export.
#
# cJSON symbol exports (source/module.cpp) are used on every platform, but the managed-component
# target name only resolves in an ESP-IDF build; POSIX links the repo's own cJSON library instead.
set(REQUIRES_LIST TactilityKernel)
if (ESP_PLATFORM)
    list(APPEND REQUIRES_LIST espressif__cjson)
else ()
    list(APPEND REQUIRES_LIST cJSON)
endif ()

tactility_add_module(cjson-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES ${REQUIRES_LIST}
)
