project(AppModuleTests)

enable_language(C CXX ASM)


file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(AppModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(AppModuleTests PRIVATE ${DOCTESTINC} ${CMAKE_CURRENT_LIST_DIR}/../private)

add_test(NAME AppModuleTests COMMAND AppModuleTests)

# No --wrap flags or stdio_wrap.cpp source needed here: linking app-module below already brings
# both along (see its own CMakeLists.txt).

target_link_libraries(AppModuleTests PUBLIC
    TactilityKernel
    TactilityKernelCpp
    app-module
    service-module
    platform-posix
    freertos_kernel
)
