cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED ENV{ESP_IDF_VERSION})

    file(GLOB_RECURSE SOURCES "Source/*.c*")
    file(GLOB_RECURSE HEADERS "Source/*.h*")

    add_library(simulator OBJECT)

    target_sources(simulator
        PRIVATE ${SOURCES}
        PUBLIC ${HEADERS}
    )

    target_link_libraries(simulator
        PRIVATE Tactility
        PRIVATE TactilityCore
        PRIVATE lvgl
        PRIVATE SDL2-static
    )

    target_link_libraries(simulator PRIVATE ${SDL2_LIBRARIES})

endif()
