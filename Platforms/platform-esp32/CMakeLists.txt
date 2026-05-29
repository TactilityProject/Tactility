cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

# Exclude USB host sources on SoCs that don't have USB OTG
file(GLOB USB_SOURCES "source/drivers/usb/*.c**")
if(NOT CONFIG_SOC_USB_OTG_SUPPORTED)
    list(REMOVE_ITEM SOURCES ${USB_SOURCES})
endif()

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include/"
    PRIV_INCLUDE_DIRS "private/"
    REQUIRES TactilityKernel driver vfs fatfs
)

idf_component_optional_requires(PRIVATE bt)
if(CONFIG_SOC_USB_OTG_SUPPORTED)
    idf_component_optional_requires(PRIVATE usb espressif__usb_host_hid espressif__usb_host_msc lvgl__lvgl espressif__esp_lvgl_port)
endif()
