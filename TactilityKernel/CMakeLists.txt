cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "Source/*.c**")

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "Include/"
        # TODO move the related logic for esp_time in Tactility/time.h into the Platform/ subproject
        REQUIRES esp_timer
    )

else ()

    add_library(TactilityKernel OBJECT ${SOURCES})
    target_include_directories(TactilityKernel PUBLIC Include/)
    target_link_libraries(TactilityKernel PUBLIC freertos_kernel)

endif ()
