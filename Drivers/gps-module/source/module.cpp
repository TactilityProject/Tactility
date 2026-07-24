// SPDX-License-Identifier: Apache-2.0
#include "gps_service_internal.h"

#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>

constexpr auto* TAG = "GpsModule";

extern "C" {

extern Driver gps_driver;

static Driver* const gps_drivers[] = {
    &gps_driver,
    nullptr
};

static error_t start() {
    error_t error = gps_service_register();
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to register GPS service: %s", error_to_string(error));
        return error;
    }
    return ERROR_NONE;
}

Module gps_module = {
    .name = "gps",
    .start = start,
    .stop = nullptr,
    .drivers = gps_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
