project(LocalisationModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(LocalisationModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(LocalisationModuleTests PRIVATE ${DOCTESTINC} ${PROJECT_SOURCE_DIR}/../private)

add_test(NAME LocalisationModuleTests COMMAND LocalisationModuleTests)

target_link_libraries(LocalisationModuleTests PUBLIC
    localisation-module
    TactilityKernel
    platform-posix
    freertos_kernel
)
