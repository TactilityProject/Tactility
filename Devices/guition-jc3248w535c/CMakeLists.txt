file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility EspLcdCompat esp_lcd_axs15231b PwmBacklight driver vfs fatfs audio-codec-module dummy-i2s-amp-module audio-stream-module
)
