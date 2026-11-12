idf_component_register(
    INCLUDE_DIRS
        "Libraries/TactilityKernel/include"
        "Libraries/TactilityKernel/include-cpp"
        "Libraries/TactilityFreeRtos/Include"
        "Libraries/lvgl/include"
        "Libraries/minmea/include"
        "Libraries/minitar/include"
        "Modules/lvgl-module/include"
    REQUIRES esp_timer app-module crypt-module gps-module lvgl-module lvgl-window-manager-module service-module
)

# Regular and core features
add_prebuilt_library(TactilityKernel Libraries/TactilityKernel/binary/libTactilityKernel.a)
add_prebuilt_library(lvgl Libraries/lvgl/binary/liblvgl.a)
add_prebuilt_library(minmea Libraries/minmea/binary/libminmea.a)
add_prebuilt_library(minitar Libraries/minitar/binary/libminitar.a)

target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityKernel)
target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl)
target_link_libraries(${COMPONENT_LIB} INTERFACE minmea)
target_link_libraries(${COMPONENT_LIB} INTERFACE minitar)
