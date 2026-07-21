file(GLOB_RECURSE SOURCE_FILES source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "source"
    REQUIRES Tactility driver esp_adc platform-esp32 lilygo-module
)
