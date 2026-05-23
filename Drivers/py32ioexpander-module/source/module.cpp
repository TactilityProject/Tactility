// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver py32ioexpander_driver;

static error_t start() {
    check(driver_construct_add(&py32ioexpander_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&py32ioexpander_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module py32ioexpander_module = {
    .name = "py32ioexpander",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
