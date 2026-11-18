file(GLOB_RECURSE SOURCE_FILES source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "source"
    REQUIRES TactilityKernel Tactility TactilityFreeRtos lvgl-module lvgl-window-manager-module
)

# Board-local link-time hooks (see source/lvgl_scaling.cpp and source/lvgl_insets.cpp): keep the
# Material theme's widget sizes usable while tightening its spacing, and keep the UI clear of the
# top/bottom edges. No core Tactility/LVGL source is modified.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=system_bars_configure" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_display_create" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_style_set_pad_top" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_style_set_pad_bottom" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_style_set_pad_left" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_style_set_pad_right" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_style_set_pad_row" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_style_set_pad_column" APPEND)
