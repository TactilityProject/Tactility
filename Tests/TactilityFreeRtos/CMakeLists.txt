project(TactilityFreeRtosTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/Source/*.cpp)
add_executable(TactilityFreeRtosTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(TactilityFreeRtosTests PRIVATE ${DOCTESTINC})

add_test(NAME TactilityFreeRtosTests COMMAND TactilityFreeRtosTests)

target_link_libraries(TactilityFreeRtosTests PUBLIC
    TactilityFreeRtos
    freertos_kernel
)
