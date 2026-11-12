project(TactilityTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/Source/*.cpp)

# Compile Tactility's own sources directly rather than linking the Tactility executable target.
# Main.cpp is excluded - it pulls in the device's Module/devicetree symbols, which would drag
# the whole device stack into the test binary just to get plain helpers like file/String.
file(GLOB_RECURSE TACTILITY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../Source/*.c*)
list(FILTER TACTILITY_SOURCES EXCLUDE REGEX ".*/Main\\.cpp$")

# Tactility's embedded assets, see Tactility/CMakeLists.txt
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
add_custom_target(TestsGeneratedAssets
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/generate-assets.py"
        "${CMAKE_SOURCE_DIR}/Data/system" "${GENERATED_DIR}"
    BYPRODUCTS "${GENERATED_DIR}/generated_assets.c" "${GENERATED_DIR}/generated_assets.h"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_custom_target(TestsGeneratedFonts
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/generate-fonts.py"
        "${CMAKE_SOURCE_DIR}/Devices/simulator" 1 "${GENERATED_DIR}"
    BYPRODUCTS "${GENERATED_DIR}/generated_fonts.c" "${GENERATED_DIR}/generated_fonts.h"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_source_files_properties("${GENERATED_DIR}/generated_assets.c" PROPERTIES GENERATED TRUE)
set_source_files_properties("${GENERATED_DIR}/generated_fonts.c" PROPERTIES GENERATED TRUE)

add_executable(TactilityTests EXCLUDE_FROM_ALL ${TEST_SOURCES} ${TACTILITY_SOURCES} "${GENERATED_DIR}/generated_assets.c" "${GENERATED_DIR}/generated_fonts.c")
add_dependencies(TactilityTests TestsGeneratedAssets TestsGeneratedFonts)
target_include_directories(TactilityTests PRIVATE "${GENERATED_DIR}")

target_include_directories(TactilityTests PRIVATE ${DOCTESTINC})
target_include_directories(TactilityTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../Include)
target_include_directories(TactilityTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../Private)

# Tactility's own CMakeLists.txt derives this from sdkconfig, but this target only ever builds for
# the POSIX simulator (see Tests/tests.md), so it just hardcodes that same file's own POSIX default
# rather than reimplementing the sdkconfig lookup here too.
target_compile_definitions(TactilityTests PRIVATE
    "-DTT_FONT_DEFAULT_SIZE=14"
    "-DTACTILITY_TEST_DATA_DIR=\"${CMAKE_CURRENT_LIST_DIR}/../../Data\""
    "-DTACTILITY_TEST_FONTS_DIR=\"${CMAKE_CURRENT_LIST_DIR}/../Fonts\""
)

add_test(NAME TactilityTests COMMAND TactilityTests)

# No --wrap flags declared here: linking platform-posix and app-module below already brings all
# of pthread_attr_setstack/read/write/close/the printf family along (see their own CMakeLists.txt
# files).

target_link_libraries(TactilityTests PRIVATE
    TactilityKernel
    TactilityFreeRtos
    platform-posix
    app-posix-module
    lvgl-module
    lvgl-window-manager-module
    app-module
    app-hub-module
    coreutils-module
    audio-decoder-module
    c-symbols-module
    cjson-module
    cpp-symbols-module
    crypt-module
    development-module
    freertos-module
    gps-module
    graphics-module
    http-module
    location-module
    mbedtls-module
    mesh-module
    mesh-meshcore-module
    mesh-meshtastic-module
    posix-symbols-module
    pthread-module
    gps-generic-module
    gps-meshtastic-module
    service-module
    bluetooth-module
    wifi-module
    lv_screenshot
    minitar
    lvgl
    cJSON
    SDL2::SDL2-static SDL2-static
)
