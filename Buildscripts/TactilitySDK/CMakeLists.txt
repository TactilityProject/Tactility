idf_component_register(
    INCLUDE_DIRS
        "Libraries/TactilityC/include"
        "Libraries/TactilityKernel/include"
        "Libraries/TactilityFreeRtos/include"
        "Libraries/lvgl/include"
        "Libraries/lvgl-module/include"
    REQUIRES esp_timer
)

add_prebuilt_library(TactilityC Libraries/TactilityC/binary/libTactilityC.a)
add_prebuilt_library(TactilityKernel Libraries/TactilityKernel/binary/libTactilityKernel.a)
add_prebuilt_library(lvgl Libraries/lvgl/binary/liblvgl.a)
add_prebuilt_library(lvgl-module Libraries/lvgl-module/binary/liblvgl-module.a)

target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityC)
target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityKernel)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl-module)
