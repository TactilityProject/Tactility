idf_component_register(
    SRC_DIRS "Source"
    INCLUDE_DIRS "Source"
    REQUIRES Tactility epdiy esp_lvgl_port
    PRIV_REQUIRES esp_timer
)
