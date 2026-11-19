project(NotificationModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(NotificationModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(NotificationModuleTests PRIVATE
    ${DOCTESTINC}
)

add_test(NAME NotificationModuleTests COMMAND NotificationModuleTests)

target_link_libraries(NotificationModuleTests PUBLIC
    TactilityKernel
    platform-posix
    notification-module
    mock-drivers-module
)
