idf_component_register(
    INCLUDE_DIRS
        "Libraries/TactilityC/include"
        "Libraries/TactilityKernel/include"
        "Libraries/lvgl/include"
        "Modules/lvgl-module/include"
        "Drivers/bm8563-module/include"
        "Drivers/bmi270-module/include"
        "Drivers/mpu6886-module/include"
        "Drivers/pi4ioe5v6408-module/include"
        "Drivers/qmi8658-module/include"
        "Drivers/rx8130ce-module/include"
    REQUIRES esp_timer
)

# Regular and core features
add_prebuilt_library(TactilityC Libraries/TactilityC/binary/libTactilityC.a)
add_prebuilt_library(TactilityKernel Libraries/TactilityKernel/binary/libTactilityKernel.a)
add_prebuilt_library(lvgl Libraries/lvgl/binary/liblvgl.a)

target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityC)
target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityKernel)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl)
