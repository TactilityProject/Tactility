cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(app-posix-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    PRIV_INCLUDE_DIRS private/
    REQUIRES TactilityKernel app-module service-module
    PRIV_REQUIRES ${CMAKE_DL_LIBS}
    # The libc overrides in source/stdio_wrap*.cpp replace libc's own by name and are never referenced
    # directly, and app-module's io.cpp calls back into their __real_read/__real_write/__real_close
    WHOLE_ARCHIVE
)

# Baked in at compile time, mirroring ESP32's CONFIG_IDF_TARGET - names the installed-app binary
# (e.g. "elf/posix-x86_64.so") this Tactility process's own architecture can dlopen(), letting one
# .app package bundle a variant per posix architecture, same as one .app bundles a .elf per chip.
target_compile_definitions(app-posix-module PRIVATE "TACTILITY_POSIX_ARCH=\"${CMAKE_SYSTEM_PROCESSOR}\"")
