#include <Tactility/lvgl/Theme.h>

#include <lvgl/theme.h>

namespace tt::lvgl {

void configureTheme(const settings::appearance::AppearanceSettings& settings) {
    LvglThemeSettings theme_settings;
    lvgl_theme_get_default_settings(&theme_settings);
    if (settings.themeMode != settings::appearance::ThemeMode::DeviceDefault) {
        theme_settings.is_dark = settings.themeMode == settings::appearance::ThemeMode::Dark;
    }
    theme_settings.is_mono = settings.monoTheme;
    if (settings.densityMode != settings::appearance::DensityMode::DeviceDefault) {
        theme_settings.is_compact = settings.densityMode == settings::appearance::DensityMode::Compact;
    }
    theme_settings.animations_enabled = settings.animationsEnabled;
    if (settings.primaryColor.has_value()) {
        theme_settings.color_primary = lv_color_hex(*settings.primaryColor);
    }
    if (settings.secondaryColor.has_value()) {
        theme_settings.color_secondary = lv_color_hex(*settings.secondaryColor);
    }
    if (settings.errorColor.has_value()) {
        theme_settings.color_error = lv_color_hex(*settings.errorColor);
    }
    if (settings.surfaceColor.has_value()) {
        theme_settings.surface_override = true;
        theme_settings.color_surface = lv_color_hex(settings::appearance::getSurfaceColor(*settings.surfaceColor, settings.surfaceTintLevel, theme_settings.is_dark));
    }
    lvgl_theme_set_settings(&theme_settings);
}

}
