project(LodePngModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(LodePngModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(LodePngModuleTests PRIVATE ${DOCTESTINC})

target_compile_definitions(LodePngModuleTests PRIVATE
    "LODEPNG_TEST_LOGO=\"${CMAKE_SOURCE_DIR}/Data/system/app/Boot/assets/logo.png\""
)

add_test(NAME LodePngModuleTests COMMAND LodePngModuleTests)

target_link_libraries(LodePngModuleTests PUBLIC
    lodepng-module
    TactilityKernel
    freertos_kernel
)
