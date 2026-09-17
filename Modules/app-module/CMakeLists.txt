cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(app-module
    SRCS ${SOURCE_FILES}
    PRIV_INCLUDE_DIRS private/
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel service-module minitar
    PRIV_REQUIRES TactilityKernelCpp
)

# Tells source/io.cpp its real-syscall fallback must go through __real_read/write/close() rather
# than calling ::read/::write/::close() directly, on every platform where those are wrapped: via
# -Wl,--wrap= on ESP-IDF (see the top-level CMakeLists.txt) and non-Apple POSIX (below), or via
# source/stdio_wrap.cpp's dyld interpose on Apple (whose linker lacks --wrap).
tactility_get_module_name(app-module MODULE_NAME)
target_compile_definitions(${MODULE_NAME} PRIVATE TT_APP_IO_WRAPS_STDIO)

# Routes every read()/write()/close() and printf-family call to source/stdio_wrap.cpp's __wrap_*
# functions; see that file for what they do and why. ESP-IDF wraps the same symbols itself (see
# the top-level CMakeLists.txt); Apple gets the same routing via dyld interpose instead, self-
# registered in that file. PUBLIC so every consumer of this module (Tactility, and any test target
# that links app-module directly) gets the flags too.
if (NOT ESP_PLATFORM AND NOT APPLE)
    tactility_get_module_name(app-module MODULE_NAME)
    target_link_options(${MODULE_NAME} PUBLIC
        "-Wl,--wrap=read" "-Wl,--wrap=write" "-Wl,--wrap=close"
        "-Wl,--wrap=printf" "-Wl,--wrap=fprintf" "-Wl,--wrap=vprintf" "-Wl,--wrap=vfprintf"
        "-Wl,--wrap=puts" "-Wl,--wrap=fputs" "-Wl,--wrap=putchar" "-Wl,--wrap=fputc"
        "-Wl,--wrap=getchar" "-Wl,--wrap=fgetc" "-Wl,--wrap=fgets"
    )
endif ()

# install.cpp resolves an installed binary's fixed bin/<platform>/<binary>.so path itself, so it
# needs the same platform-arch string app-posix-module's own CMakeLists.txt defines for its
# loader (the ESP32 equivalent, CONFIG_IDF_TARGET, comes for free from sdkconfig.h there).
if (NOT ESP_PLATFORM)
    tactility_get_module_name(app-module MODULE_NAME)
    target_compile_definitions(${MODULE_NAME} PRIVATE "TACTILITY_POSIX_ARCH=\"${CMAKE_SYSTEM_PROCESSOR}\"")
endif ()
