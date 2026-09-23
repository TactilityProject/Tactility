cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(app-module
    SRCS ${SOURCE_FILES}
    PRIV_INCLUDE_DIRS private/
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel service-module minitar
    PRIV_REQUIRES TactilityKernelCpp
    WHOLE_ARCHIVE
)

# Tells source/io.cpp its real-syscall fallback must go through __real_read/write/close() rather
# than calling ::read/::write/::close() directly, on every platform where those are wrapped: via
# -Wl,--wrap= on ESP-IDF (see the top-level CMakeLists.txt), or via source/stdio_wrap.cpp's own
# read/write/close and printf-family definitions on POSIX (dyld interpose on Apple, plain
# symbol overrides elsewhere - see that file for why POSIX doesn't use --wrap).
tactility_get_module_name(app-module MODULE_NAME)
target_compile_definitions(${MODULE_NAME} PRIVATE TT_APP_IO_WRAPS_STDIO)

# install.cpp resolves an installed binary's fixed bin/<platform>/<binary>.so path itself, so it
# needs the same platform-arch string app-posix-module's own CMakeLists.txt defines for its
# loader (the ESP32 equivalent, CONFIG_IDF_TARGET, comes for free from sdkconfig.h there).
if (NOT ESP_PLATFORM)
    tactility_get_module_name(app-module MODULE_NAME)
    target_compile_definitions(${MODULE_NAME} PRIVATE "TACTILITY_POSIX_ARCH=\"${CMAKE_SYSTEM_PROCESSOR}\"")
endif ()
