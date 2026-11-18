#include <Tactility/lvgl/FontSizes.h>

#include <lvgl/theme.h>

#include <cmath>

namespace tt::lvgl {

static uint16_t scale(uint16_t size, float factor) {
    return static_cast<uint16_t>(std::lround(size * factor));
}

uint16_t getDefaultFontSize() {
    const uint16_t size = lvgl_theme_get_default_font_size();
    return size != 0 ? size : static_cast<uint16_t>(TT_FONT_DEFAULT_SIZE);
}

uint16_t getTextFontSize(uint16_t defaultSize, LvglFontSize fontSize) {
    switch (fontSize) {
        case FONT_SIZE_SMALL: return scale(defaultSize, 0.75f);
        case FONT_SIZE_LARGE: return scale(defaultSize, 1.25f);
        case FONT_SIZE_DEFAULT:
        default: return defaultSize;
    }
}

uint16_t getIconFontSize(uint16_t defaultSize, LvglIconFont iconFont) {
    switch (iconFont) {
        case LVGL_ICON_FONT_LAUNCHER: return scale(defaultSize, 2.6f);
        case LVGL_ICON_FONT_SHARED_DEFAULT: return scale(defaultSize, 1.25);
        case LVGL_ICON_FONT_SHARED_LARGE: return getIconFontSize(defaultSize, LVGL_ICON_FONT_SHARED) * 2;
        case LVGL_ICON_FONT_STATUSBAR:
        default: return scale(defaultSize, 1.15f);
    }
}

}
