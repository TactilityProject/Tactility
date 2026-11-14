cmake_minimum_required(VERSION 3.20)

if (DEFINED ENV{ESP_IDF_VERSION})
    idf_component_register(
        INCLUDE_DIRS "src/"
    )
else()
    add_library(nanosvg INTERFACE)
    target_include_directories(nanosvg INTERFACE "src/")
endif()
