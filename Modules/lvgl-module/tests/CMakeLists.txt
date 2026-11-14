project(LvglModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(LvglModuleTests EXCLUDE_FROM_ALL
    ${TEST_SOURCES}
    ${PROJECT_SOURCE_DIR}/../source/insets_math.cpp
    ${PROJECT_SOURCE_DIR}/../source/widgets/qr.cpp
    ${PROJECT_SOURCE_DIR}/../source/svg.c
    # LVGL allocates through the kernel
    ${PROJECT_SOURCE_DIR}/../source/lv_mem_custom.c
)

target_include_directories(LvglModuleTests PRIVATE
    ${DOCTESTINC}
    ${PROJECT_SOURCE_DIR}/../include
    ${PROJECT_SOURCE_DIR}/../private
)

target_compile_definitions(LvglModuleTests PRIVATE APP_ICON_DIR="${PROJECT_SOURCE_DIR}/../../../Data/system/app_icon" FILE_ICON_DIR="${PROJECT_SOURCE_DIR}/../../../Data/system/file_icon")

add_test(NAME LvglModuleTests COMMAND LvglModuleTests)

target_link_libraries(LvglModuleTests PUBLIC
    TactilityKernel
    lvgl
    nanosvg
    QRCode
)
