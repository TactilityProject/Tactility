file(GLOB_RECURSE SOURCE_FILES source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    REQUIRES TactilityKernel driver esp_driver_gpio
)
