// SPDX-License-Identifier: Apache-2.0
#include <gps/gps.h>
#include <gps/module.h>
#include <gps/private/gps_ledger.h>

#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

// UARTs can appear and disappear at runtime, so their GPS devices follow them
static void on_device_event(Device* device, DeviceEvent event, void*) {
    if (device_get_type(device) != &UART_CONTROLLER_TYPE) {
        return;
    }
    if (event == DEVICE_EVENT_STARTED) {
        gps_ledger_sync();
    } else if (event == DEVICE_EVENT_STOPPING) {
        gps_ledger_remove_children_of(device);
    }
}

static error_t start() {
    // Materializes devices for configurations persisted in previous sessions.
    gps_ledger_sync();
    device_listener_add(on_device_event, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    device_listener_remove(on_device_event, nullptr);
    gps_ledger_clear();
    return ERROR_NONE;
}

static const ModuleSymbol gps_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(GPS_TYPE),
    DEFINE_MODULE_SYMBOL(gps_event_subscribe),
    DEFINE_MODULE_SYMBOL(gps_event_unsubscribe),
    DEFINE_MODULE_SYMBOL(gps_event_await),
    DEFINE_MODULE_SYMBOL(gps_get_state),
    DEFINE_MODULE_SYMBOL(gps_get_model_name),
    DEFINE_MODULE_SYMBOL(gps_model_to_string),
    MODULE_SYMBOL_TERMINATOR
};

Module gps_module = {
    .name = "gps",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = gps_module_symbols,
    .internal = nullptr,
};

}
