project(LvglModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(LvglModuleTests EXCLUDE_FROM_ALL
    ${TEST_SOURCES}
    ${PROJECT_SOURCE_DIR}/../source/insets_math.cpp
)

target_include_directories(LvglModuleTests PRIVATE
    ${DOCTESTINC}
    ${PROJECT_SOURCE_DIR}/../include
    ${PROJECT_SOURCE_DIR}/../private
)

add_test(NAME LvglModuleTests COMMAND LvglModuleTests)

target_link_libraries(LvglModuleTests PUBLIC
    TactilityKernel
    lvgl
)
