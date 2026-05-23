file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility esp_lvgl_port ILI934x FT6x36 AXP2101 AW9523 driver vfs fatfs ina226-module py32ioexpander-module
)
