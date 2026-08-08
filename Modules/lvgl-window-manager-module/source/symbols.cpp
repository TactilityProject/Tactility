// SPDX-License-Identifier: Apache-2.0
#include <lvgl_window_manager/module.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

const ModuleSymbol lvgl_window_manager_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(window_manager_create),
    DEFINE_MODULE_SYMBOL(window_manager_remove),
    DEFINE_MODULE_SYMBOL(window_manager_get_state),
    DEFINE_MODULE_SYMBOL(window_manager_await_state_change),
    // terminator
    MODULE_SYMBOL_TERMINATOR
};

Module lvgl_window_manager_module = {
    .name = "lvgl-window-manager",
    .start = window_manager_start,
    .stop = window_manager_stop,
    .drivers = nullptr,
    .symbols = lvgl_window_manager_module_symbols,
    .internal = nullptr
};

}
