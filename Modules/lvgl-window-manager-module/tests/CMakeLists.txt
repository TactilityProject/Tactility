project(LvglWindowManagerModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(LvglWindowManagerModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(LvglWindowManagerModuleTests PRIVATE
    ${DOCTESTINC}
    ${CMAKE_CURRENT_LIST_DIR}/../source
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(LvglWindowManagerModuleTests PRIVATE DIALOG_START_WRAPPING)
    target_link_options(LvglWindowManagerModuleTests PRIVATE -Wl,--wrap=thread_alloc_full -Wl,--wrap=thread_start)
endif()

add_test(NAME LvglWindowManagerModuleTests COMMAND LvglWindowManagerModuleTests)

target_link_libraries(LvglWindowManagerModuleTests PUBLIC
    TactilityKernel
    freertos_kernel
    platform-posix
    lvgl
    lvgl-module
    lvgl-window-manager-module
)