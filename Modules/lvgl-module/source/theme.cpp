// SPDX-License-Identifier: Apache-2.0
#include <lvgl/theme.h>
#include <lvgl/theme_private.h>

#include <lvgl/lvgl.h>
#include <tactility/log.h>

#include "themes/lv_theme_material.h"
#include "themes/lv_theme_material_mono.h"

constexpr auto* TAG = "lvgl_theme";

enum class ActiveTheme {
    None,
    Material,
    MaterialMono
};

static LvglThemeSettings theme_settings;
static bool theme_settings_set = false;
static ActiveTheme active_theme = ActiveTheme::None;

void lvgl_theme_get_default_settings(LvglThemeSettings* settings) {
    lv_theme_material_config_t material_config;
    lv_theme_material_config_init(&material_config);
#if defined(LV_THEME_DEFAULT_DARK) && LV_THEME_DEFAULT_DARK
    settings->is_dark = true;
#else
    settings->is_dark = false;
#endif
    settings->is_mono = false;
    settings->animations_enabled = true;
    settings->color_primary = material_config.color_primary;
    settings->color_secondary = material_config.color_secondary;
    settings->color_error = material_config.color_error;
}

void lvgl_theme_get_settings(LvglThemeSettings* settings) {
    if (theme_settings_set) {
        *settings = theme_settings;
    } else {
        lvgl_theme_get_default_settings(settings);
    }
}

void lvgl_theme_set_settings(const LvglThemeSettings* settings) {
    theme_settings = *settings;
    theme_settings_set = true;
}

static lv_theme_t* get_active_theme() {
    switch (active_theme) {
        case ActiveTheme::Material:
            return lv_theme_material_get();
        case ActiveTheme::MaterialMono:
            return lv_theme_material_mono_get();
        default:
            return nullptr;
    }
}

void lvgl_theme_deinit() {
    switch (active_theme) {
        case ActiveTheme::Material:
            lv_theme_material_deinit();
            break;
        case ActiveTheme::MaterialMono:
            lv_theme_material_mono_deinit();
            break;
        default:
            break;
    }
    active_theme = ActiveTheme::None;
}

lv_theme_t* lvgl_theme_init_for_display(lv_display_t* display, bool isMonoDisplay, bool supportsAnimations) {
    LvglThemeSettings settings;
    lvgl_theme_get_settings(&settings);
    const bool is_compact = lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT;
    const bool animations_enabled = settings.animations_enabled && supportsAnimations;
    const ActiveTheme required_theme = (isMonoDisplay || settings.is_mono) ? ActiveTheme::MaterialMono : ActiveTheme::Material;

    // Only one theme is allocated at a time
    lv_theme_t* replaced_theme = nullptr;
    if (active_theme != ActiveTheme::None && active_theme != required_theme) {
        replaced_theme = get_active_theme();
        lvgl_theme_deinit();
    }

    lv_theme_t* theme;
    if (required_theme == ActiveTheme::MaterialMono) {
        lv_theme_material_mono_config_t config;
        lv_theme_material_mono_config_init(&config);
        config.is_dark = settings.is_dark;
        config.is_compact = is_compact;
        config.animations_enabled = animations_enabled;
        theme = lv_theme_material_mono_init(display, &config);
    } else {
        lv_theme_material_config_t config;
        lv_theme_material_config_init(&config);
        config.is_dark = settings.is_dark;
        config.is_compact = is_compact;
        config.animations_enabled = animations_enabled;
        config.color_primary = settings.color_primary;
        config.color_secondary = settings.color_secondary;
        config.color_error = settings.color_error;
        theme = lv_theme_material_init(display, &config);
    }
    active_theme = required_theme;

    // Other displays mustn't keep the deinitialized theme. Their widgets keep the old styles until they're recreated.
    if (replaced_theme != nullptr) {
        LOG_W(TAG, "Displays need different themes: switching all displays to the new theme");
        lv_display_t* other_display = lv_display_get_next(nullptr);
        while (other_display != nullptr) {
            if (lv_display_get_theme(other_display) == replaced_theme) {
                lv_display_set_theme(other_display, theme);
            }
            other_display = lv_display_get_next(other_display);
        }
    }

    return theme;
}
