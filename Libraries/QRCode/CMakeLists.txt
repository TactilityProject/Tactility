cmake_minimum_required(VERSION 3.20)

if (DEFINED ENV{ESP_IDF_VERSION})
    file(GLOB_RECURSE SOURCE_FILES src/*.c)

    idf_component_register(
        SRCS ${SOURCE_FILES}
        INCLUDE_DIRS "src/"
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-unknown-pragmas)
    endif()
else()
    file(GLOB SOURCES "src/*.c")
    file(GLOB HEADERS "src/*.h")

    add_library(QRCode STATIC)

    target_sources(QRCode
        PRIVATE ${SOURCES}
        PUBLIC ${HEADERS}
    )

    target_include_directories(QRCode
        PUBLIC src
    )

    # qrcode.h polyfills bool/true/false for pre-C23 compilers - on a host compiler that
    # defaults to C23 (where bool is a keyword), that polyfill itself fails to compile. Pin to
    # C11 for the simulator build only; ESP-IDF's own toolchain default is unaffected.
    set_target_properties(QRCode PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
endif()
