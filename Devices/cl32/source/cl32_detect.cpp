// SPDX-License-Identifier: Apache-2.0
#include "cl32_detect.h"

#include "cl32_v2_keyboard.h"
#include "cl32_v3_keyboard.h"
#include "cl32_v2.h"
#include "cl32_v3.h"
#include "cl32_v4.h"
#include "cl32_v4_keyboard.h"
#include "cl32_v4_power.h"

#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <cstring>

constexpr auto* TAG = "cl32-detect";

// Revision 2 and 3 boards are both plain tca8418 keyboards (different physical key layouts) with
// no core chip. Revision 4 boards have the core chip (power + keyboard) at CL32_V4_CORE_I2C_ADDRESS.
static Cl32HardwareRevision cl32_revision = Cl32HardwareRevision::Unknown;

static Device cl32_v4_power_device {};
static bool cl32_v4_power_created = false;

// The probe-once latch for on_i2c0_started(). File-scope (not function-local) so
// cl32_teardown_devices() can reset it for a later start/probe cycle.
static bool did_probe = false;

static void create_v2_devices(Device* i2c0) {
    cl32_create_keyboard(i2c0);
}

static void create_v3_devices(Device* i2c0) {
    cl32_v3_create_keyboard(i2c0);
}

static bool create_v4_power_device(Device* i2c0) {
    cl32_v4_power_device = Device { .address = 0, .name = "cl32-v4-power", .config = nullptr, .parent = nullptr, .flags = 0, .internal = nullptr };

    error_t error = device_construct(&cl32_v4_power_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct cl32-v4-power: %s", error_to_string(error));
        return false;
    }

    device_set_parent(&cl32_v4_power_device, i2c0);
    device_set_driver(&cl32_v4_power_device, &cl32_v4_power_driver);

    error = device_add(&cl32_v4_power_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add cl32-v4-power: %s", error_to_string(error));
        device_destruct(&cl32_v4_power_device);
        return false;
    }

    error = device_start(&cl32_v4_power_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start cl32-v4-power: %s", error_to_string(error));
        device_remove(&cl32_v4_power_device);
        device_destruct(&cl32_v4_power_device);
        return false;
    }

    return true;
}

static void create_v4_devices(Device* i2c0) {
    cl32_v4_create_keyboard(i2c0);
    cl32_v4_power_created = create_v4_power_device(i2c0);
}

// Stops, removes and destructs cl32_v4_power_device if it was successfully created.
// cl32_v4_power_driver's own stop() tears down its power-supply child device.
static void destroy_v4_power_device() {
    if (!cl32_v4_power_created) {
        return;
    }
    device_stop(&cl32_v4_power_device);
    device_remove(&cl32_v4_power_device);
    device_destruct(&cl32_v4_power_device);
    cl32_v4_power_created = false;
}

static Cl32HardwareRevision cl32_detect(Device* i2c0) {
    uint8_t probe_value = 0;
    if (i2c_controller_register8_get(i2c0, CL32_V4_CORE_I2C_ADDRESS, CL32_V4_KEYBOARD_PROBE_REGISTER, &probe_value, CL32_V4_CORE_TIMEOUT) == ERROR_NONE) {
        LOG_I(TAG, "Detected V4 hardware by V4 keyboard presence");
        return Cl32HardwareRevision::Revision4;
    }

    if (i2c_controller_has_device_at_address(i2c0, CL32_V3_FUEL_GAUGE_I2C_ADDRESS, CL32_V3_TIMEOUT) == ERROR_NONE) {
        LOG_I(TAG, "Detected V3 hardware by MAX17048G fuel gauge presence");
        return Cl32HardwareRevision::Revision3;
    }

    LOG_I(TAG, "No core chip or fuel gauge detected, assuming revision 2 hardware");
    return Cl32HardwareRevision::Revision2;
}

// Fires for every device's start/stop in the system.
static void on_i2c0_started(Device* device, DeviceEvent event, void* context) {
    (void)context;

    if (did_probe || event != DEVICE_EVENT_STARTED || strcmp(device->name, "i2c0") != 0) {
        return;
    }
    did_probe = true;

    cl32_revision = cl32_detect(device);

    switch (cl32_revision) {
        case Cl32HardwareRevision::Revision2:
            create_v2_devices(device);
            break;
        case Cl32HardwareRevision::Revision3:
            create_v3_devices(device);
            break;
        case Cl32HardwareRevision::Revision4:
            create_v4_devices(device);
            break;
        default:
            LOG_W(TAG, "Unknown/unsupported hardware revision");
            break;
    }
}

Cl32HardwareRevision cl32_hardware_revision() {
    return cl32_revision;
}

void cl32_teardown_devices() {
    // Revision 2/3 keyboards bind the shared ti,tca8418 driver (owned by tca8418-module), so they
    // don't block cl32_module's own driver destruction - torn down anyway for symmetry and so a
    // later start can recreate them cleanly.
    cl32_destroy_keyboard();
    cl32_v3_destroy_keyboard();

    // Revision 4 devices bind cl32-owned drivers (cl32_v4_keyboard_driver, cl32_v4_power_driver).
    // Must be torn down before cl32_module's driver-removal loop runs, or driver_destruct() finds
    // a started device still using the driver and fails.
    cl32_v4_destroy_keyboard();
    destroy_v4_power_device();

    did_probe = false;
}

void cl32_power_detect_start() {
    device_listener_add(on_i2c0_started, nullptr);
}

void cl32_power_detect_stop() {
    device_listener_remove(on_i2c0_started);
}
