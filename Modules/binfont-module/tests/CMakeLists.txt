project(BinFontModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(BinFontModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(BinFontModuleTests PRIVATE ${DOCTESTINC} ${PROJECT_SOURCE_DIR}/../private)

target_compile_definitions(BinFontModuleTests PRIVATE
    "BINFONT_TEST_FIXTURES_DIR=\"${PROJECT_SOURCE_DIR}/fixtures\""
    "BINFONT_TEST_TEMP_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\""
    "BINFONT_TEST_ICON_TTF=\"${CMAKE_SOURCE_DIR}/Data/system/fonts/MaterialSymbolsRounded.ttf\""
)

add_test(NAME BinFontModuleTests COMMAND BinFontModuleTests)

target_link_libraries(BinFontModuleTests PUBLIC
    binfont-module
    graphics-module
    TactilityKernel
    freertos_kernel
)
