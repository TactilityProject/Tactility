// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** User-configurable theme settings */
typedef struct {
    bool is_dark;
    /** Use the monochrome theme, also on colour displays. Monochrome and greyscale displays always use it. */
    bool is_mono;
    /** Slow-refresh displays (e.g. e-paper) never show animations */
    bool animations_enabled;
    lv_color_t color_primary;
    lv_color_t color_secondary;
    lv_color_t color_error;
} LvglThemeSettings;

/**
 * @brief Gets the theme settings, or the defaults when no settings were set.
 * @param[out] settings the current theme settings
 */
void lvgl_theme_get_settings(LvglThemeSettings* settings);

/**
 * @brief Gets the default theme settings for this device.
 * @param[out] settings the default theme settings
 */
void lvgl_theme_get_default_settings(LvglThemeSettings* settings);

/**
 * @brief Sets the theme settings. They are used for displays that are added afterwards, e.g. when LVGL (re)starts.
 * @param[in] settings the theme settings
 */
void lvgl_theme_set_settings(const LvglThemeSettings* settings);

#ifdef __cplusplus
}
#endif
