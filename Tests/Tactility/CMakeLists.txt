project(TactilityCoreTests)

enable_language(C CXX ASM)

set(CMAKE_CXX_COMPILER g++)

file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/*.cpp)
add_executable(TactilityTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

add_definitions(-D_Nullable=)
add_definitions(-D_Nonnull=)

target_include_directories(TactilityTests PRIVATE
    ${DOCTESTINC}
)

add_test(NAME TactilityTests
    COMMAND TactilityTests
)

target_link_libraries(TactilityTests PRIVATE
    Tactility
    TactilityCore
    TactilityKernel
    PlatformPosix
    hal-device-module
    lvgl-module
    lvgl
    Simulator
    SDL2::SDL2-static SDL2-static
)
