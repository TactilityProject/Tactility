project(TactilityTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/Source/*.cpp)

# Compile Tactility's own sources directly rather than linking the Tactility executable target.
# Main.cpp is excluded - it pulls in the device's Module/devicetree symbols, which would drag
# the whole device stack into the test binary just to get plain helpers like file/String.
file(GLOB_RECURSE TACTILITY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../Source/*.c*)
list(FILTER TACTILITY_SOURCES EXCLUDE REGEX ".*/Main\\.cpp$")

add_executable(TactilityTests EXCLUDE_FROM_ALL ${TEST_SOURCES} ${TACTILITY_SOURCES})

target_include_directories(TactilityTests PRIVATE ${DOCTESTINC})
target_include_directories(TactilityTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../Include)
target_include_directories(TactilityTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../Private)

add_test(NAME TactilityTests COMMAND TactilityTests)

# Matches Tactility/CMakeLists.txt's own set of --wrap flags: this binary also compiles
# AppStdioWrap.cpp and links app-module (whose io.cpp calls __real_read/write/close() on every
# POSIX platform - see TT_APP_IO_WRAPS_STDIO in Modules/app-module/CMakeLists.txt), so it needs
# the same wraps applied on non-Apple, or those go unresolved; AppStdioWrap.cpp provides its own
# on Apple via dyld interpose (see that file).
if (NOT APPLE)
    target_link_options(TactilityTests PRIVATE
        "-Wl,--wrap=pthread_attr_setstack" "-Wl,--wrap=read" "-Wl,--wrap=write" "-Wl,--wrap=close"
        "-Wl,--wrap=printf" "-Wl,--wrap=fprintf" "-Wl,--wrap=vprintf" "-Wl,--wrap=vfprintf"
        "-Wl,--wrap=puts" "-Wl,--wrap=fputs" "-Wl,--wrap=putchar" "-Wl,--wrap=fputc"
        "-Wl,--wrap=getchar" "-Wl,--wrap=fgetc" "-Wl,--wrap=fgets"
    )
endif ()

target_link_libraries(TactilityTests PRIVATE
    TactilityKernel
    TactilityKernelCpp
    TactilityFreeRtos
    platform-posix
    app-posix-module
    lvgl-module
    lvgl-window-manager-module
    app-module
    c-symbols-module
    cpp-symbols-module
    crypt-module
    freertos-module
    gps-module
    http-module
    mbedtls-module
    posix-symbols-module
    pthread-module
    gps-generic-module
    gps-meshtastic-module
    service-module
    lv_screenshot
    minitar
    lvgl
    cJSON
    SDL2::SDL2-static SDL2-static
)
