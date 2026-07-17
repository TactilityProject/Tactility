// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver drv2605_driver;

static error_t start() {
    check(driver_construct_add(&drv2605_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&drv2605_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module drv2605_module = {
    .name = "drv2605",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
