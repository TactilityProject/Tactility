// SPDX-License-Identifier: Apache-2.0
#include <lvgl_window_manager/dialog.h>
#include <lvgl_window_manager/module.h>
#include <lvgl_window_manager/system_bars.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/module.h>

extern "C" {

static const ModuleSymbol SYMBOLS[] = {
    DEFINE_MODULE_SYMBOL(window_manager_create),
    DEFINE_MODULE_SYMBOL(window_manager_create_ext),
    DEFINE_MODULE_SYMBOL(window_manager_remove),
    DEFINE_MODULE_SYMBOL(window_manager_get_state),
    DEFINE_MODULE_SYMBOL(window_manager_await_state_change),
    DEFINE_MODULE_SYMBOL(window_manager_overlay_show),
    DEFINE_MODULE_SYMBOL(window_manager_overlay_hide),
    DEFINE_MODULE_SYMBOL(window_manager_get_active_layer),
    DEFINE_MODULE_SYMBOL(window_manager_alert_dialog),
    DEFINE_MODULE_SYMBOL(window_manager_input_dialog),
    DEFINE_MODULE_SYMBOL(window_manager_selection_dialog),
    DEFINE_MODULE_SYMBOL(window_manager_dialog_dismiss),
    DEFINE_MODULE_SYMBOL(system_bars_refresh),
    DEFINE_MODULE_SYMBOL(system_bars_add_pointer),
    DEFINE_MODULE_SYMBOL(system_bars_set_statusbar_visible),
    DEFINE_MODULE_SYMBOL(system_bars_get_layout),
    DEFINE_MODULE_SYMBOL(system_bars_get_alignment),
    DEFINE_MODULE_SYMBOL(system_bars_set_main_action),
    DEFINE_MODULE_SYMBOL(system_bars_set_page_button_callbacks),
    DEFINE_MODULE_SYMBOL(system_bars_set_page_buttons_visible),
    DEFINE_MODULE_SYMBOL(system_bars_set_notification_visible),
    DEFINE_MODULE_SYMBOL(system_bars_set_notification_unread),
    DEFINE_MODULE_SYMBOL(system_bars_is_menu_swipe_zone_pressed),
    MODULE_SYMBOL_TERMINATOR,
};

Module lvgl_window_manager_module = {
    .name = "lvgl-window-manager",
    .start = window_manager_start,
    .stop = window_manager_stop,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
