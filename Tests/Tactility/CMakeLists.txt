project(TactilityTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/Source/*.cpp)
add_executable(TactilityTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(TactilityTests PRIVATE ${DOCTESTINC})

add_test(NAME TactilityTests COMMAND TactilityTests)

target_link_libraries(TactilityTests PRIVATE
    Tactility
    TactilityKernel
    platform-posix
    lvgl-module
    crypt-module
    gps-module
    lvgl
    SDL2::SDL2-static SDL2-static
)
