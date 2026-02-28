file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility esp_lvgl_port esp_lcd EspLcdCompat esp_lcd_ili9881c GT911 PwmBacklight driver vfs fatfs
)
