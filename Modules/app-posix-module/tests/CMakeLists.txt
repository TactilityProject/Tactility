project(AppPosixModuleTests)

enable_language(C CXX ASM)

# Fixture: a tiny shared object with a main() the test dlopen()s through app-posix-module's real
# loader service. Deliberately not linked against app-module, so its call into
# app_scheduler_current_app_id() stays undefined until dlopen() resolves it against
# AppPosixModuleTests's own copy, exactly like a real Tactility-embedded app would resolve
# against the running Tactility binary.
add_library(app_posix_module_test_fixture SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/fixtures/fixture_app.cpp)
target_include_directories(app_posix_module_test_fixture PRIVATE ${CMAKE_SOURCE_DIR}/Modules/app-module/include)
set_target_properties(app_posix_module_test_fixture PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Fixture: calls printf() with no app-module linkage of its own, so its call must resolve against
# the host process's own printf - proves a dlopen()ed app's stdio actually reaches app_io_write()
# rather than bypassing straight to libc (see ../source/stdio_wrap.cpp's own comment for why that's not a
# given on POSIX).
add_library(printf_fixture SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/fixtures/printf_fixture.cpp)
set_target_properties(printf_fixture PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Fixture: calls exit() with a live std::string on its stack, resolving exit() against the host's override.
add_library(exit_fixture SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/fixtures/exit_fixture.cpp)
set_target_properties(exit_fixture PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Fixture: leaks files, memory and optionally a thread, for APP_MANIFEST_FLAG_CLEANUP tests.
add_library(leak_fixture SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/fixtures/leak_fixture.cpp)
set_target_properties(leak_fixture PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Fixture: static objects whose destructors free and close what the app acquired, when it's unloaded.
add_library(static_fixture SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/fixtures/static_fixture.cpp)
set_target_properties(static_fixture PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Fixture: makes path-based calls with relative paths, which must resolve against the app's own cwd.
add_library(paths_fixture SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/fixtures/paths_fixture.cpp)
target_include_directories(paths_fixture PRIVATE ${CMAKE_SOURCE_DIR}/TactilityKernel/include)
set_target_properties(paths_fixture PROPERTIES POSITION_INDEPENDENT_CODE ON)

# A file with a ".so" extension but no ELF header, for is_executable() rejection tests.
set(NON_ELF_FIXTURE_PATH "${CMAKE_CURRENT_BINARY_DIR}/not-elf.so")
file(WRITE "${NON_ELF_FIXTURE_PATH}" "not an elf file")

# An install-directory-shaped fixture: {dir}/bin/posix-{arch}/app.so, matching where
# app_posix_loader_service.cpp's resolve_app_path() looks for a "packaged" app's single binary.
set(INSTALL_DIR_FIXTURE_PATH "${CMAKE_CURRENT_BINARY_DIR}/install-dir-fixture")
set(INSTALL_DIR_FIXTURE_BIN_DIR "${INSTALL_DIR_FIXTURE_PATH}/bin/posix-${CMAKE_SYSTEM_PROCESSOR}")
add_custom_command(
    OUTPUT "${INSTALL_DIR_FIXTURE_BIN_DIR}/app.so"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALL_DIR_FIXTURE_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:app_posix_module_test_fixture>" "${INSTALL_DIR_FIXTURE_BIN_DIR}/app.so"
    DEPENDS app_posix_module_test_fixture
)
add_custom_target(install_dir_fixture DEPENDS "${INSTALL_DIR_FIXTURE_BIN_DIR}/app.so")

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(AppPosixModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})
add_dependencies(AppPosixModuleTests app_posix_module_test_fixture printf_fixture exit_fixture leak_fixture paths_fixture static_fixture install_dir_fixture)

target_include_directories(AppPosixModuleTests PRIVATE ${DOCTESTINC})
target_compile_definitions(AppPosixModuleTests PRIVATE
    FIXTURE_APP_PATH="$<TARGET_FILE:app_posix_module_test_fixture>"
    PRINTF_FIXTURE_APP_PATH="$<TARGET_FILE:printf_fixture>"
    EXIT_FIXTURE_APP_PATH="$<TARGET_FILE:exit_fixture>"
    LEAK_FIXTURE_APP_PATH="$<TARGET_FILE:leak_fixture>"
    PATHS_FIXTURE_APP_PATH="$<TARGET_FILE:paths_fixture>"
    STATIC_FIXTURE_APP_PATH="$<TARGET_FILE:static_fixture>"
    FIXTURE_NON_ELF_PATH="${NON_ELF_FIXTURE_PATH}"
    FIXTURE_INSTALL_DIR_PATH="${INSTALL_DIR_FIXTURE_PATH}"
)

add_test(NAME AppPosixModuleTests COMMAND AppPosixModuleTests)

target_link_libraries(AppPosixModuleTests PUBLIC
    TactilityKernel
    app-module
    app-posix-module
    service-module
    platform-posix
    freertos_kernel
    ${CMAKE_DL_LIBS}
)
# So the fixture's undefined app_scheduler_current_app_id() reference can resolve against this
# test binary's own copy at dlopen() time. See app-posix-module's own ENABLE_EXPORTS comment on
# the real Tactility executable for why this is needed.
set_target_properties(AppPosixModuleTests PROPERTIES ENABLE_EXPORTS ON)
