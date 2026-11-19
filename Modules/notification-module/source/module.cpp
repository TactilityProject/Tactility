// SPDX-License-Identifier: Apache-2.0
#include <notification/module.h>
#include <notification/notification.h>
#include <notification/notification_state.h>

#include <tactility/module.h>

extern "C" {

static error_t start() {
    return notification_state_start();
}

static error_t stop() {
    notification_state_stop();
    return ERROR_NONE;
}

static const ModuleSymbol notification_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(notification_release),
    DEFINE_MODULE_SYMBOL(notification_context_for_app),
    DEFINE_MODULE_SYMBOL(notification_context_for_service),
    DEFINE_MODULE_SYMBOL(notification_context_set_title),
    DEFINE_MODULE_SYMBOL(notification_context_set_severity),
    DEFINE_MODULE_SYMBOL(notification_context_get_data),
    DEFINE_MODULE_SYMBOL(notification_context_set_action),
    DEFINE_MODULE_SYMBOL(notification_context_set_sticky),
    DEFINE_MODULE_SYMBOL(notification_send),
    DEFINE_MODULE_SYMBOL(notification_update),
    DEFINE_MODULE_SYMBOL(notification_preload),
    DEFINE_MODULE_SYMBOL(notification_dismiss_unless_sticky),
    DEFINE_MODULE_SYMBOL(notification_dismiss_non_sticky),
    DEFINE_MODULE_SYMBOL(notification_context_for_notification),
    DEFINE_MODULE_SYMBOL(notification_context_destroy),
    DEFINE_MODULE_SYMBOL(notification_get),
    DEFINE_MODULE_SYMBOL(notification_get_all),
    DEFINE_MODULE_SYMBOL(notification_get_count),
    DEFINE_MODULE_SYMBOL(notification_get_sources),
    DEFINE_MODULE_SYMBOL(notification_set_source_enabled),
    DEFINE_MODULE_SYMBOL(notification_dismiss),
    DEFINE_MODULE_SYMBOL(notification_invoke_action),
    DEFINE_MODULE_SYMBOL(notification_dismiss_all),
    DEFINE_MODULE_SYMBOL(notification_subscribe),
    DEFINE_MODULE_SYMBOL(notification_unsubscribe),
    DEFINE_MODULE_SYMBOL(notification_poll),
    MODULE_SYMBOL_TERMINATOR
};

Module notification_module = {
    .name = "notification",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = notification_module_symbols,
    .internal = nullptr,
};

}
