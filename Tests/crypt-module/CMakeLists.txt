project(CryptModuleTests)

enable_language(C CXX ASM)

file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/Source/*.cpp)
add_executable(CryptModuleTests EXCLUDE_FROM_ALL ${TEST_SOURCES})

target_include_directories(CryptModuleTests PRIVATE ${DOCTESTINC})

add_test(NAME CryptModuleTests COMMAND CryptModuleTests)

target_link_libraries(CryptModuleTests PUBLIC
    crypt-module
    TactilityKernel
    freertos_kernel
)
