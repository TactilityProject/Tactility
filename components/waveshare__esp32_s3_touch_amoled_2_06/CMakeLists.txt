idf_component_register(
    SRCS "esp32_s3_touch_amoled_2_06.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver esp_driver_i2c esp_driver_gpio esp_lcd esp_codec_dev
    PRIV_REQUIRES esp_timer spiffs esp_psram fatfs usb
)
