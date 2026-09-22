project(AudioStreamModuleTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(AudioStreamModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(AudioStreamModuleTests PRIVATE ${DOCTESTINC})

add_test(NAME AudioStreamModuleTests COMMAND AudioStreamModuleTests)

target_link_libraries(AudioStreamModuleTests PUBLIC
    TactilityKernel
    audio-stream-module
    platform-posix
    freertos_kernel
)
