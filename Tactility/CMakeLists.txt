cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

list(APPEND REQUIRES_LIST
    TactilityKernel
    TactilityCore
    TactilityFreeRtos
    hal-device-module
    lvgl-module
    lv_screenshot
    minitar
    minmea
)

if (DEFINED ENV{ESP_IDF_VERSION})

    list(APPEND REQUIRES_LIST
        PlatformEsp32
        driver
        elf_loader
        QRCode
        esp_http_server
        esp_http_client
        esp-tls
        esp_wifi
        json # Effectively cJSON
        nvs_flash
        spiffs
        vfs
        fatfs
        lwip
        spi_flash
    )

    if ("${IDF_TARGET}" STREQUAL "esp32s3")
        list(APPEND REQUIRES_LIST esp_tinyusb)
    endif ()

else ()

    list(APPEND REQUIRES_LIST
        PlatformPosix
        freertos_kernel
        cJSON
        lvgl
    )

endif ()

tactility_add_module(Tactility
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS Include/
    PRIV_INCLUDE_DIRS Private/
    REQUIRES ${REQUIRES_LIST}
)

if (DEFINED ENV{ESP_IDF_VERSION})
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-unused-variable)
    endif ()

    if (NOT DEFINED TACTILITY_SKIP_SPIFFS)
        # Read-only
        fatfs_create_rawflash_image(system "${CMAKE_CURRENT_SOURCE_DIR}/../Data/system" FLASH_IN_PROJECT PRESERVE_TIME)
        # Read-write
        fatfs_create_spiflash_image(data "${CMAKE_CURRENT_SOURCE_DIR}/../Data/data" FLASH_IN_PROJECT PRESERVE_TIME)
    endif ()

endif ()

