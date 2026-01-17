file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_C_STANDARD 23)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility esp_lcd ST7796 BQ25896 BQ27220 TCA8418 DRV2605 PwmBacklight driver esp_adc drivers-esp
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=c23)
