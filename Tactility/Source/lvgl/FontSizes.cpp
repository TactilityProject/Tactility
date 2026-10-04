#include <Tactility/lvgl/FontSizes.h>

#include <cmath>

namespace tt::lvgl {

static uint16_t scaleDefaultSize(float factor) {
    return static_cast<uint16_t>(std::lround(TT_FONT_DEFAULT_SIZE * factor));
}

uint16_t getTextFontSize(LvglFontSize fontSize) {
    switch (fontSize) {
        case FONT_SIZE_SMALL: return scaleDefaultSize(0.75f);
        case FONT_SIZE_LARGE: return scaleDefaultSize(1.25f);
        case FONT_SIZE_DEFAULT:
        default: return TT_FONT_DEFAULT_SIZE;
    }
}

uint16_t getIconFontSize(LvglIconFont iconFont) {
    switch (iconFont) {
        case LVGL_ICON_FONT_LAUNCHER: return scaleDefaultSize(2.6f);
        case LVGL_ICON_FONT_SHARED_2X: return getIconFontSize(LVGL_ICON_FONT_SHARED) * 2;
        case LVGL_ICON_FONT_STATUSBAR:
        case LVGL_ICON_FONT_SHARED:
        default: return scaleDefaultSize(1.15f);
    }
}

}
