// SPDX-License-Identifier: Apache-2.0
#include "cl32_v3.h"
#include "cl32_v3_fuel_gauge.h"

#include <drivers/max17048.h>

#include <tactility/driver.h>
#include <tactility/log.h>

constexpr auto* TAG = "cl32-v3-fuel-gauge";

static Max17048Config cl32_v3_fuel_gauge_config {};
static Device cl32_v3_fuel_gauge_device {};
static bool cl32_v3_fuel_gauge_created = false;

bool cl32_v3_create_fuel_gauge(Device* i2c0) {
    cl32_v3_fuel_gauge_config = Max17048Config {
        .address = CL32_V3_FUEL_GAUGE_I2C_ADDRESS,
    };

    cl32_v3_fuel_gauge_device = Device {
        .address = 0,
        .name = "cl32-v3-fuel-gauge",
        .config = &cl32_v3_fuel_gauge_config,
        .parent = nullptr,
        .flags = 0,
        .internal = nullptr,
    };

    error_t error = device_construct(&cl32_v3_fuel_gauge_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct the fuel gauge: %s", error_to_string(error));
        return false;
    }

    device_set_parent(&cl32_v3_fuel_gauge_device, i2c0);

    Driver* driver = driver_find_compatible("maxim,max17048");
    if (driver == nullptr) {
        LOG_E(TAG, "No driver registered for maxim,max17048");
        device_destruct(&cl32_v3_fuel_gauge_device);
        return false;
    }
    device_set_driver(&cl32_v3_fuel_gauge_device, driver);

    error = device_add(&cl32_v3_fuel_gauge_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add the fuel gauge: %s", error_to_string(error));
        device_destruct(&cl32_v3_fuel_gauge_device);
        return false;
    }

    error = device_start(&cl32_v3_fuel_gauge_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start the fuel gauge: %s", error_to_string(error));
        device_remove(&cl32_v3_fuel_gauge_device);
        device_destruct(&cl32_v3_fuel_gauge_device);
        return false;
    }

    cl32_v3_fuel_gauge_created = true;
    return true;
}

void cl32_v3_destroy_fuel_gauge() {
    if (!cl32_v3_fuel_gauge_created) {
        return;
    }
    device_stop(&cl32_v3_fuel_gauge_device);
    device_remove(&cl32_v3_fuel_gauge_device);
    device_destruct(&cl32_v3_fuel_gauge_device);
    cl32_v3_fuel_gauge_created = false;
}
