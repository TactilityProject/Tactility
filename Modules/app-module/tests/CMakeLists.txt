project(AppModuleTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(AppModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

# Provides __wrap_read/write/close: paired with the -Wl,--wrap= below on non-Apple platforms (see
# Tactility/CMakeLists.txt), or self-registered via dyld interpose on Apple (see the file itself).
target_sources(AppModuleTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../Tactility/Source/AppStdioWrap.cpp)

target_include_directories(AppModuleTests PRIVATE ${DOCTESTINC} ${CMAKE_CURRENT_LIST_DIR}/../private)

add_test(NAME AppModuleTests COMMAND AppModuleTests)

# Matches app-module's own TT_APP_IO_WRAPS_STDIO (see Modules/app-module/CMakeLists.txt): io.cpp
# calls __real_read/write/close() on every POSIX platform, so any final link of it needs these
# wraps too, or those symbols go unresolved. The printf-family/getc-family flags are needed for
# the same reason: AppStdioWrap.cpp compiles those __wrap_* functions unconditionally on this
# platform, and they reference __real_vfprintf/fputs/fputc/fgetc/fgets. --wrap provides all of
# these __real_* symbols on non-Apple; AppStdioWrap.cpp provides its own on Apple (see that file).
if (NOT APPLE)
    target_link_options(AppModuleTests PRIVATE
        "-Wl,--wrap=read" "-Wl,--wrap=write" "-Wl,--wrap=close"
        "-Wl,--wrap=printf" "-Wl,--wrap=fprintf" "-Wl,--wrap=vprintf" "-Wl,--wrap=vfprintf"
        "-Wl,--wrap=puts" "-Wl,--wrap=fputs" "-Wl,--wrap=putchar" "-Wl,--wrap=fputc"
        "-Wl,--wrap=getchar" "-Wl,--wrap=fgetc" "-Wl,--wrap=fgets"
    )
endif ()

target_link_libraries(AppModuleTests PUBLIC
    TactilityKernel
    app-module
    service-module
    platform-posix
    freertos_kernel
)
