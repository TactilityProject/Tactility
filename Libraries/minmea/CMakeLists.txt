cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (DEFINED ENV{ESP_IDF_VERSION})
    idf_component_register(
        SRC_DIRS "Source/"
        INCLUDE_DIRS "Include/"
    )

else()
    file(GLOB SOURCES "Source/*.c*")
    file(GLOB HEADERS "Include/*.h*")

    add_library(minmea STATIC)

    target_sources(minmea PRIVATE ${SOURCES})
    include_directories(minmea "Include/")
    target_include_directories(minmea PUBLIC "Include/")

endif()
