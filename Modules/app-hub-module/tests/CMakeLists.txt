project(AppHubModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(AppHubModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(AppHubModuleTests PRIVATE
    ${DOCTESTINC}
    ${PROJECT_SOURCE_DIR}/../private
)

add_test(NAME AppHubModuleTests COMMAND AppHubModuleTests)

target_link_libraries(AppHubModuleTests PUBLIC
    TactilityKernel
    platform-posix
    app-module
    app-posix-module
    service-module
    app-hub-module
)
