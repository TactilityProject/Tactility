file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility esp_lvgl_port esp_lcd RgbDisplay GT911 PwmBacklight driver vfs fatfs audio-codec-module dummy-i2s-amp-module audio-stream-module
)
