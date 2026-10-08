// SPDX-License-Identifier: Apache-2.0
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>

enum UiDensity lvgl_get_ui_density(void) {
    return lvgl_theme_is_compact() ? LVGL_UI_DENSITY_COMPACT : LVGL_UI_DENSITY_DEFAULT;
}
