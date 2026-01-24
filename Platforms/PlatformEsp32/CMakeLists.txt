cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "Source/*.c**")

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "Include/"
        REQUIRES TactilityKernel driver
    )

else ()

    add_library(PlatformEsp32 OBJECT)
    target_sources(PlatformEsp32 PRIVATE ${SOURCES})
    target_include_directories(PlatformEsp32 PUBLIC Include/)
    target_link_libraries(PlatformEsp32 PUBLIC TactilityKernel)

endif ()