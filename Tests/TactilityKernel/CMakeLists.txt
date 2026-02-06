project(TactilityCoreTests)

enable_language(C CXX ASM)

set(CMAKE_CXX_COMPILER g++)

file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/Source/*.cpp)
add_executable(TactilityKernelTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(TactilityKernelTests PRIVATE ${DOCTESTINC})

add_test(NAME TactilityKernelTests COMMAND TactilityKernelTests)

target_link_libraries(TactilityKernelTests PUBLIC
    TactilityKernel
    PlatformPosix
)
