idf_component_register(
    INCLUDE_DIRS
        "Libraries/TactilityC/include"
        "Libraries/TactilityKernel/include"
        "Libraries/TactilityFreeRtos/Include"
        "Libraries/lvgl/include"
        "Libraries/minmea/include"
        "Modules/lvgl-module/include"
        # DRIVER_INCLUDE_DIRS_PLACEHOLDER
    REQUIRES esp_timer minitar app-module crypt-module gps-module lvgl-module lvgl-window-manager-module service-module
)

# Regular and core features
add_prebuilt_library(TactilityC Libraries/TactilityC/binary/libTactilityC.a)
add_prebuilt_library(TactilityKernel Libraries/TactilityKernel/binary/libTactilityKernel.a)
add_prebuilt_library(lvgl Libraries/lvgl/binary/liblvgl.a)
add_prebuilt_library(minmea Libraries/minmea/binary/libminmea.a)

target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityC)
target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityKernel)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl)
target_link_libraries(${COMPONENT_LIB} INTERFACE minmea)
