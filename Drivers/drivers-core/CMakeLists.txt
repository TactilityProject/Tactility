cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "include/"
    )

    target_compile_options(${COMPONENT_LIB} PRIVATE -std=c23)

else ()

    add_library(drivers-core OBJECT)
    target_sources(drivers-core PRIVATE ${SOURCES})
    target_include_directories(drivers-core PUBLIC include/)
    target_compile_options(drivers-core PRIVATE -std=c23)

endif ()
