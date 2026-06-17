file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility esp_lvgl_port esp_lcd EspLcdCompat esp_lcd_ili9881c esp_lcd_st7123 esp_lcd_touch_st7123 GT911Ng PwmBacklight driver esp_driver_i2c vfs fatfs ina226-module
)
