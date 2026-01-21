cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "include/"
        REQUIRES TactilityKernel driver
    )

else ()

    add_library(drivers-esp OBJECT)
    target_sources(drivers-esp PRIVATE ${SOURCES})
    target_include_directories(drivers-esp PUBLIC include/)
    target_link_libraries(drivers-esp PUBLIC core)

endif ()