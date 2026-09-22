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
    audio-decoder-module
    c-symbols-module
    cjson-module
    cpp-symbols-module
    crypt-module
    font-module
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
