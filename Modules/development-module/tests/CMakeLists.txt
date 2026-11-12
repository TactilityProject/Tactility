project(DevelopmentModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(DevelopmentModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(DevelopmentModuleTests PRIVATE
    ${DOCTESTINC}
    ${PROJECT_SOURCE_DIR}/../private
)

add_test(NAME DevelopmentModuleTests COMMAND DevelopmentModuleTests)

target_link_libraries(DevelopmentModuleTests PUBLIC
    TactilityKernel
    platform-posix
    app-module
    app-posix-module
    service-module
    development-module
    mbedtls
)
