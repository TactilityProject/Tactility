#include "drivers/sdl_display.h"

#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <cstring>

constexpr auto* TAG = "Simulator";

extern "C" {

extern Driver sdl_display_driver;
extern Driver sdl_pointer_driver;
extern Driver sdl_keyboard_driver;

static Driver* const simulator_drivers[] = {
    &sdl_display_driver,
    &sdl_pointer_driver,
    &sdl_keyboard_driver,
    nullptr
};

}

// These devices have no real bus to attach to (SDL has no notion of one), but every non-root
// device is still expected to have a parent (see Device::parent) - they're parented to root once
// it's available below.
static const SdlDisplayConfig sdl_display_config = { 320, 240 };
static Device sdl_display_device {};
static Device sdl_pointer_device {};
static Device sdl_keyboard_device {};

static bool construct_add_start(Device* device, Device* parent, const char* name, const void* config, const char* compatible) {
    device->address = 0;
    device->name = name;
    device->config = config;
    device->parent = nullptr;
    device->internal = nullptr;

    error_t error = device_construct(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct %s: %s", name, error_to_string(error));
        return false;
    }

    device_set_parent(device, parent);

    Driver* driver = driver_find_compatible(compatible);
    if (driver == nullptr) {
        LOG_E(TAG, "No driver registered for %s", compatible);
        device_destruct(device);
        return false;
    }
    device_set_driver(device, driver);

    if (device_add(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to add %s", name);
        device_destruct(device);
        return false;
    }

    if (device_start(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start %s", name);
        device_remove(device);
        device_destruct(device);
        return false;
    }

    return true;
}

// Root is only constructed/added/started after all dts_modules (including this one) have already
// started (see kernel_init()), so it can't be looked up by name from this module's own start() -
// wait for its DEVICE_EVENT_STARTED instead, same as e.g. m5stack-tab5's display/keyboard detection.
static void on_root_started(Device* device, DeviceEvent event, void* context) {
    if (event != DEVICE_EVENT_STARTED || strcmp(device->name, "/") != 0) {
        return;
    }

    construct_add_start(&sdl_display_device, device, "display0", &sdl_display_config, "tactility,sdl-display");
    construct_add_start(&sdl_pointer_device, device, "pointer0", nullptr, "tactility,sdl-pointer");
    construct_add_start(&sdl_keyboard_device, device, "keyboard0", nullptr, "tactility,sdl-keyboard");
}

extern "C" {

static error_t start() {
    device_listener_add(on_root_started, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    device_listener_remove(on_root_started);
    return ERROR_NONE;
}

Module simulator_module = {
    .name = "simulator",
    .start = start,
    .stop = stop,
    .drivers = simulator_drivers
};

}
