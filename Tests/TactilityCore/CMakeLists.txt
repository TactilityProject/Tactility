project(TactilityCoreTests)

enable_language(C CXX ASM)

set(CMAKE_CXX_COMPILER g++)

file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/*.cpp)
add_executable(TactilityCoreTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(TactilityCoreTests PRIVATE
    ${DOCTESTINC}
)

add_test(NAME TactilityCoreTests
    COMMAND TactilityCoreTests
)

target_link_libraries(TactilityCoreTests PUBLIC
    TactilityCore
    TactilityKernel
    TactilityFreeRtos
    freertos_kernel
)
