cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "include/"
    )

else ()

    add_library(core OBJECT ${SOURCES})
    target_include_directories(core PUBLIC include/)
    target_link_libraries(core PUBLIC freertos_kernel)

endif ()
