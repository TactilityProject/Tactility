idf_component_register(
    INCLUDE_DIRS
        "Libraries/TactilityC/include"
        "Libraries/TactilityKernel/include"
        "Libraries/lvgl/include"
    REQUIRES esp_timer
)

set(EXTRA_COMPONENT_DIRS
    "Libraries/TactilityFreeRtos"
    "Modules"
    "Drivers"
)

set(COMPONENTS
    TactilityFreeRtos
    bm8563-module
    bm8563-module
    bmi270-module
    mpu6886-module
    pi4ioe5v6408-module
    qmi8658-module
    rx8130ce-module
)

# Regular and core features
add_prebuilt_library(TactilityC Libraries/TactilityC/binary/libTactilityC.a)
add_prebuilt_library(TactilityKernel Libraries/TactilityKernel/binary/libTactilityKernel.a)
add_prebuilt_library(lvgl Libraries/lvgl/binary/liblvgl.a)

target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityC)
target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityKernel)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl)
