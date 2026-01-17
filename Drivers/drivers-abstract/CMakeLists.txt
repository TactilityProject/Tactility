cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "include/"
        REQUIRES drivers-core
    )

    target_compile_options(${COMPONENT_LIB} PRIVATE -std=c23)

else ()

    add_library(drivers-abstract OBJECT)
    target_sources(drivers-abstract PRIVATE ${SOURCES})
    target_include_directories(drivers-abstract PUBLIC include/)
    target_link_libraries(drivers-abstract PUBLIC drivers-core)
    target_compile_options(drivers-abstract PRIVATE -std=c23)

endif ()
