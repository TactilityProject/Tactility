file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility esp_lvgl_port esp_lcd esp_lcd_ili9881c esp_lcd_st7123 esp_lcd_touch_st7123 esp_lcd_touch_gt911 driver esp_driver_i2c vfs fatfs ina226-module ili9881c-module st7123-module gt911-module
)
