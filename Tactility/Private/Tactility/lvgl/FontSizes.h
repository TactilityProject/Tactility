#pragma once

#include <lvgl/fonts.h>

#include <cstdint>

namespace tt::lvgl {

/** @return the size in pixels of a text font, derived from the default font size */
uint16_t getTextFontSize(LvglFontSize fontSize);

/** @return the size in pixels of an icon font, derived from the default font size */
uint16_t getIconFontSize(LvglIconFont iconFont);

}
