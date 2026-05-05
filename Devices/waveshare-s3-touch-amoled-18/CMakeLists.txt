file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

file(GLOB_RECURSE SOURCE_FILES "Source/*.cpp")

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility ButtonControl esp_lvgl_port esp32_s3_touch_amoled_2_06 PwmBacklight driver esp_driver_i2c esp_driver_gpio esp_lcd esp_timer spiffs esp_psram fatfs usb espressif__esp_lcd_touch_ft5x06
    PRIV_REQUIRES espressif__esp_lcd_panel_io_additions waveshare__esp_lcd_sh8601
)
