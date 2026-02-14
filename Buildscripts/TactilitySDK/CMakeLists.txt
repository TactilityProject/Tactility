idf_component_register(
    INCLUDE_DIRS
        "Libraries/TactilityC/Include"
        "Libraries/TactilityKernel/Include"
        "Libraries/TactilityFreeRtos/Include"
        "Libraries/lvgl/Include"
        "Libraries/lvgl-module/include"
    REQUIRES esp_timer
)

add_prebuilt_library(TactilityC Libraries/TactilityC/Binary/libTactilityC.a)
add_prebuilt_library(TactilityKernel Libraries/TactilityKernel/Binary/libTactilityKernel.a)
add_prebuilt_library(lvgl Libraries/lvgl/Binary/liblvgl.a)
add_prebuilt_library(lvgl-module Libraries/lvgl-module/Binary/liblvgl-module.a)

target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityC)
target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityKernel)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl-module)
