cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        INCLUDE_DIRS "Include/"
        REQUIRES esp_timer
    )

else()

    add_library(TactilityFreeRtos INTERFACE)

    target_include_directories(TactilityFreeRtos
        INTERFACE Include/
    )

    target_link_libraries(TactilityFreeRtos
        INTERFACE freertos_kernel
    )
endif()