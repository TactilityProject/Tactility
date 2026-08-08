project(AppModuleTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/Source/*.cpp)
add_executable(AppModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(AppModuleTests PRIVATE ${DOCTESTINC})

add_test(NAME AppModuleTests COMMAND AppModuleTests)

target_link_libraries(AppModuleTests PUBLIC
    TactilityKernel
    app-module
    service-module
    platform-posix
    freertos_kernel
)
