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

# TerminalRenderer*.cpp needs this; Tactility's own CMakeLists.txt derives it from sdkconfig, but
# this target only ever builds for the POSIX simulator (see Tests/tests.md), so it just hardcodes
# that same file's own POSIX default rather than reimplementing the sdkconfig lookup here too.
target_compile_definitions(TactilityTests PRIVATE
    "-DTT_TERMINAL_FONT_SIZE=14"
    "-DTT_TERMINAL_FONT_SYMBOL=ibmplexmono_14_font"
)

add_test(NAME TactilityTests COMMAND TactilityTests)

# No --wrap flags declared here: linking platform-posix and app-module below already brings all
# of pthread_attr_setstack/read/write/close/the printf family along (see their own CMakeLists.txt
# files).

target_link_libraries(TactilityTests PRIVATE
    TactilityKernel
    TactilityKernelCpp
    TactilityFreeRtos
    platform-posix
    app-posix-module
    lvgl-module
    lvgl-window-manager-module
    app-module
    coreutils-module
    audio-decoder-module
    c-symbols-module
    cjson-module
    cpp-symbols-module
    crypt-module
    freertos-module
    gps-module
    graphics-module
    http-module
    mbedtls-module
    posix-symbols-module
    pthread-module
    gps-generic-module
    gps-meshtastic-module
    service-module
    wifi-module
    lv_screenshot
    minitar
    lvgl
    cJSON
    SDL2::SDL2-static SDL2-static
)
