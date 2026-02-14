file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility esp_lcd ST7796 BQ25896 BQ27220 TCA8418 DRV2605 PwmBacklight driver esp_adc platform-esp32
)
