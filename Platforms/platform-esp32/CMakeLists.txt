cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include/"
    PRIV_INCLUDE_DIRS "private/"
    REQUIRES TactilityKernel driver esp_adc esp_driver_i2c vfs fatfs esp_wifi esp_netif esp_event
)

idf_component_optional_requires(PRIVATE bt usb espressif__usb_host_hid espressif__usb_host_msc)
