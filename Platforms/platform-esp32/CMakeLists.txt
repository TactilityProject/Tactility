cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include/"
    PRIV_INCLUDE_DIRS "private/"
    REQUIRES TactilityKernel driver esp_adc esp_driver_i2c esp_lcd vfs fatfs esp_wifi esp_netif esp_event
)

if (DEFINED ENV{ESP_IDF_VERSION})
    GET_PROPERTY_FILE_CONTENT("${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig" sdkconfig_text)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_ESP_HOSTED_ENABLED" esp_host_enabled "n")
    if (esp_host_enabled STREQUAL "y")
        idf_component_optional_requires(PRIVATE espressif__esp_hosted)
    endif ()
endif ()

idf_component_optional_requires(PRIVATE bt usb espressif__usb_host_hid espressif__usb_host_msc)
