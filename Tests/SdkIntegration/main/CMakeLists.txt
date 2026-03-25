file(GLOB_RECURSE SOURCE_FILES Source/*.c)

idf_component_register(
    SRCS ${SOURCE_FILES}
    REQUIRES TactilitySDK
        lvgl-module
        bm8563-module
        bmi270-module
        mpu6886-module
        pi4ioe5v6408-module
        qmi8658-module
        rx8130ce-module
)
