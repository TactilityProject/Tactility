cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "Source/*.c**")

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCES}
#        INCLUDE_DIRS "Include/"
        REQUIRES TactilityKernel driver
    )

else ()

    add_library(PlatformPosix OBJECT)
    target_sources(PlatformPosix PRIVATE ${SOURCES})
#    target_include_directories(PlatformPosix PUBLIC Include/)
    target_link_libraries(PlatformPosix PUBLIC TactilityKernel)

endif ()