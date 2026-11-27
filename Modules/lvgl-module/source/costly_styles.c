// SPDX-License-Identifier: Apache-2.0
/*
 * Styles that make LVGL render a widget and its children into an offscreen layer. A layer takes extra memory and render time,
 * and much more stack of the LVGL task, which draws recursively, so it can overflow in a deep widget tree.
 *
 * The calls to these setters are redirected here with -Wl,--wrap= (see CMakeLists.txt), including those of ESP32 apps,
 * which get the address of the wrapper from the symbol table. lv_obj_set_style_transform_scale() is covered through
 * the _x and _y functions that it calls. Apple's linker has no --wrap, so there are no warnings there.
 */
#ifndef __APPLE__

#include <tactility/log.h>

#include <lvgl.h>

#include <stdbool.h>
#include <stdint.h>

// The linker provides these
void __real_lv_obj_set_style_clip_corner(lv_obj_t* obj, bool value, lv_style_selector_t selector);
void __real_lv_obj_set_style_transform_rotation(lv_obj_t* obj, int32_t value, lv_style_selector_t selector);
void __real_lv_obj_set_style_transform_scale_x(lv_obj_t* obj, int32_t value, lv_style_selector_t selector);
void __real_lv_obj_set_style_transform_scale_y(lv_obj_t* obj, int32_t value, lv_style_selector_t selector);

static const char* const TAG = "lvgl";

/** Warns the first time only, as widgets are often styled over and over */
static void warn_once(bool* warned, const char* style, const void* caller) {
    if (!*warned) {
        *warned = true;
        LOG_W(TAG, "%s (called from %p) renders into an offscreen layer: it costs memory, render time and much LVGL task stack. Avoid it.", style, caller);
    }
}

void __wrap_lv_obj_set_style_clip_corner(lv_obj_t* obj, bool value, lv_style_selector_t selector) {
    static bool warned = false;
    if (value) {
        warn_once(&warned, "clip_corner", __builtin_return_address(0));
    }
    __real_lv_obj_set_style_clip_corner(obj, value, selector);
}

void __wrap_lv_obj_set_style_transform_rotation(lv_obj_t* obj, int32_t value, lv_style_selector_t selector) {
    static bool warned = false;
    if (value != 0) {
        warn_once(&warned, "transform_rotation", __builtin_return_address(0));
    }
    __real_lv_obj_set_style_transform_rotation(obj, value, selector);
}

void __wrap_lv_obj_set_style_transform_scale_x(lv_obj_t* obj, int32_t value, lv_style_selector_t selector) {
    static bool warned = false;
    if (value != LV_SCALE_NONE) {
        warn_once(&warned, "transform_scale_x", __builtin_return_address(0));
    }
    __real_lv_obj_set_style_transform_scale_x(obj, value, selector);
}

void __wrap_lv_obj_set_style_transform_scale_y(lv_obj_t* obj, int32_t value, lv_style_selector_t selector) {
    static bool warned = false;
    if (value != LV_SCALE_NONE) {
        warn_once(&warned, "transform_scale_y", __builtin_return_address(0));
    }
    __real_lv_obj_set_style_transform_scale_y(obj, value, selector);
}

#endif
