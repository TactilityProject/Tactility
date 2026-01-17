cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (DEFINED ENV{ESP_IDF_VERSION})
    idf_component_register(
        SRC_DIRS "Source/"
        INCLUDE_DIRS "Source/"
        PRIV_INCLUDE_DIRS "Private/"
        REQUIRES lvgl
    )

else()
    file(GLOB SOURCES "Source/*.c*")
    file(GLOB HEADERS "Source/*.h*")

    add_library(lv_screenshot STATIC)

    target_sources(lv_screenshot
        PRIVATE ${SOURCES}
        PUBLIC ${HEADERS}
    )

    target_include_directories(lv_screenshot
        PRIVATE Private
        PUBLIC Source
    )

    target_link_libraries(lv_screenshot
        PUBLIC lvgl
    )
endif()