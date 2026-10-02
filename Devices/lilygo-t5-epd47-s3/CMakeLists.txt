file(GLOB_RECURSE SOURCE_FILES source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "source"
    REQUIRES TactilityKernel esp_lcd esp_driver_gpio esp_driver_rmt esp_timer esp_rom hal soc heap freertos
)

# The pixel loops of the display are too slow with the debug optimization level of the project
set_source_files_properties(
    source/drivers/t5s3_epd.cpp
    source/drivers/t5s3_display.cpp
    PROPERTIES COMPILE_OPTIONS -O2
)
