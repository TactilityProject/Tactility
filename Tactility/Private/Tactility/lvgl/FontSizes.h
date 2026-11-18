#pragma once

#include <lvgl/fonts.h>

#include <cstdint>

namespace tt::lvgl {

/** @return the default font size in pixels of the device, before the user's choice in the Appearance settings */
uint16_t getDefaultFontSize();

/** @return the size in pixels of a text font, derived from the default font size */
uint16_t getTextFontSize(uint16_t defaultSize, LvglFontSize fontSize);

/** @return the size in pixels of an icon font, derived from the default font size */
uint16_t getIconFontSize(uint16_t defaultSize, LvglIconFont iconFont);

}
