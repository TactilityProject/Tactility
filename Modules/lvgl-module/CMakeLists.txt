cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCE_FILES}
        INCLUDE_DIRS "Include/"
        REQUIRES lvgl esp_lvgl_port
    )

else ()

    add_library(lvgl-module OBJECT)

    target_sources(lvgl-module PRIVATE ${SOURCE_FILES})

    target_include_directories(lvgl-module
        PUBLIC Include/
    )

    target_link_libraries(lvgl-module PUBLIC
        TactilityKernel
        freertos_kernel
        lvgl
    )
    
endif ()

