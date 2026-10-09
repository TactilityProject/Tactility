// SPDX-License-Identifier: Apache-2.0
#include "cl32_detect.h"

#include "cl32_v2_keyboard.h"
#include "cl32_v3_keyboard.h"
#include "cl32_v3.h"

#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <cstring>

constexpr auto* TAG = "cl32-detect";

// Revision 2 and 3 boards are both plain tca8418 keyboards with different physical key layouts.
// Revision 3 boards also have a fuel gauge.
static Cl32HardwareRevision cl32_revision = Cl32HardwareRevision::Unknown;

// The probe-once latch for on_i2c0_started(). File-scope (not function-local) so
// cl32_teardown_devices() can reset it for a later start/probe cycle.
static bool did_probe = false;

static Cl32HardwareRevision cl32_detect(Device* i2c0) {
    if (i2c_controller_has_device_at_address(i2c0, CL32_V3_FUEL_GAUGE_I2C_ADDRESS, CL32_V3_TIMEOUT) == ERROR_NONE) {
        LOG_I(TAG, "Detected V3 hardware by MAX17048G fuel gauge presence");
        return Cl32HardwareRevision::Revision3;
    }

    LOG_I(TAG, "No fuel gauge detected, assuming revision 2 hardware");
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
            cl32_create_keyboard(device);
            break;
        case Cl32HardwareRevision::Revision3:
            cl32_v3_create_keyboard(device);
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
    // The keyboards bind the shared ti,tca8418 driver (owned by tca8418-module), so they don't block
    // this module's driver destruction. They are torn down so a later start can recreate them.
    cl32_destroy_keyboard();
    cl32_v3_destroy_keyboard();

    did_probe = false;
}

void cl32_power_detect_start() {
    device_listener_add(on_i2c0_started, nullptr);
}

void cl32_power_detect_stop() {
    device_listener_remove(on_i2c0_started, nullptr);
}
