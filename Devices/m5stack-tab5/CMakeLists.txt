file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES TactilityKernel lvgl-module es838x-module ina226-module ili9881c-module st7121-module st7123-module gt911-module
)
