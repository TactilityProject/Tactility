file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility ButtonControl XPT2046SoftSPI PwmBacklight EstimatedPower ST7789-i8080 driver vfs fatfs
)
