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
    /** Padding and gaps from 0 (compact) to 100 (regular) */
    uint8_t spacing;
    /** Control sizes, corner radii and borders from 0 (compact) to 100 (regular) */
    uint8_t size;
    /** Use color_surface as the surface (screen background) colour, instead of the theme's own. Not used by the monochrome theme. */
    bool surface_override;
    lv_color_t color_surface;
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
 * @brief Overrides the compile-time default UI density, for devices that only know it at runtime.
 * Call before the theme settings are read.
 * @param[in] spacing 0 (compact) to 100 (regular)
 * @param[in] size 0 (compact) to 100 (regular)
 */
void lvgl_theme_set_default_density(uint8_t spacing, uint8_t size);

/**
 * @brief Checks whether the active theme is the monochrome theme: on monochrome and greyscale displays, or when selected in the settings.
 * @return true when the monochrome theme is active
 */
bool lvgl_theme_is_mono(void);

/**
 * @brief Gets the spacing of the theme settings: padding and gaps.
 * The device's default is defined by the UI density in its `device.properties`.
 * @return 0 (compact) to 100 (regular)
 */
uint8_t lvgl_theme_get_spacing(void);

/**
 * @brief Gets the size of the theme settings: control sizes, corner radii and borders.
 * The device's default is defined by the UI density in its `device.properties`.
 * @return 0 (compact) to 100 (regular)
 */
uint8_t lvgl_theme_get_size(void);

/**
 * @brief Interpolates between a compact and a regular value.
 * @param[in] compact the value at 0
 * @param[in] regular the value at 100
 * @param[in] percent 0 to 100, e.g. from lvgl_theme_get_spacing() or lvgl_theme_get_size()
 */
static inline int32_t lvgl_theme_lerp(int32_t compact, int32_t regular, uint8_t percent) {
    return (int32_t)(compact + ((int64_t)regular - compact) * percent / 100);
}

/**
 * @brief Sets the theme settings. They are used for displays that are added afterwards, e.g. when LVGL (re)starts.
 * @param[in] settings the theme settings
 */
void lvgl_theme_set_settings(const LvglThemeSettings* settings);

#ifdef __cplusplus
}
#endif
