#pragma once

#include <Tactility/settings/AppearanceSettings.h>

namespace tt::lvgl {

/**
 * Sets the theme settings from the appearance settings.
 * They take effect for displays that are added afterwards, so call this before LVGL (re)starts.
 */
void configureTheme(const settings::appearance::AppearanceSettings& settings);

}
