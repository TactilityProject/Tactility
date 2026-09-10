cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

set(PRIV_REQUIRES_LIST "")
if ("${IDF_TARGET}" STREQUAL "esp32s3" OR "${IDF_TARGET}" STREQUAL "esp32p4")
    list(APPEND PRIV_REQUIRES_LIST esp_tinyusb)
endif ()

if ("${IDF_TARGET}" STREQUAL "esp32p4")
    list(APPEND PRIV_REQUIRES_LIST esp_driver_ppa)
    list(APPEND PRIV_REQUIRES_LIST esp_mm)
endif ()

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include/"
    PRIV_INCLUDE_DIRS "private/"
    REQUIRES TactilityKernel TactilityKernelCpp driver esp_adc esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_uart esp_lcd vfs fatfs esp_wifi esp_netif esp_event
    PRIV_REQUIRES ${PRIV_REQUIRES_LIST}
)

if (DEFINED ENV{ESP_IDF_VERSION})
    GET_PROPERTY_FILE_CONTENT("${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig" sdkconfig_text)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_ESP_HOSTED_HOST" esp_host_enabled "n")
    if (esp_host_enabled STREQUAL "y")
        idf_component_optional_requires(PRIVATE espressif__esp_hosted)
    endif ()
endif ()

idf_component_optional_requires(PRIVATE bt usb espressif__usb_host_hid espressif__usb_host_msc)

if ("${IDF_TARGET}" STREQUAL "esp32p4")
    # Force-links esp32p4_libc_compat.c's __issignalingf() shim - nothing references it until
    # espressif__esp_ipa is scanned much later in link order, by which point this archive is
    # already passed over.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,__issignalingf")
endif ()
