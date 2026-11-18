// SPDX-License-Identifier: Apache-2.0

// Keeps this board's UI clear of the display edges with a fixed inset.
//
// Core Tactility only applies the insets it derives from the display shape for round displays (see
// Modules/lvgl-window-manager-module/source/system_bars.cpp), and this rectangular AMOLED reports no shape, so instead of
// changing core source this wraps system_bars_configure() and adds to the callback that runs once the root layout exists -
// the same board-local link-time wrapping used in lvgl_scaling.cpp.
//
// The padding is applied to the window-manager root widget (the argument of that callback), which holds the whole layout.
// That way the statusbar is inset with the rest of the UI instead of sitting on the top edge of the display.
//
// lv_obj_set_style_pad_all() writes the local style prop directly, so it is NOT affected by the
// /8 padding wrapper in lvgl_scaling.cpp (that one only covers lv_style_set_pad_*).

#include <lvgl_window_manager/system_bars.h>

#include <lvgl/lvgl.h>

#include <stdint.h>

namespace {

// Fixed insets, in pixels: horizontal on the left/right, vertical on the top/bottom.
constexpr int32_t INSET_HORIZONTAL_PX = 50;
constexpr int32_t INSET_VERTICAL_PX = 30;

// The callback that the caller of system_bars_configure() passed, if any
void (*original_on_screen_created)(lv_obj_t* root) = nullptr;

} // namespace

extern "C" {

static void wrapping_on_screen_created(lv_obj_t* root_widget) {
    lv_obj_set_style_pad_hor(root_widget, INSET_HORIZONTAL_PX, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(root_widget, INSET_VERTICAL_PX, LV_STATE_DEFAULT);
    if (original_on_screen_created != nullptr) {
        original_on_screen_created(root_widget);
    }
}

void __real_system_bars_configure(const struct SystemBarsConfig* config);

void __wrap_system_bars_configure(const struct SystemBarsConfig* config) {
    original_on_screen_created = config->on_screen_created;
    SystemBarsConfig wrapped = *config;
    wrapped.on_screen_created = wrapping_on_screen_created;
    __real_system_bars_configure(&wrapped);
}

} // extern "C"
