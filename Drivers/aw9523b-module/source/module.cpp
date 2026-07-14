// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver aw9523b_driver;

static error_t start() {
    check(driver_construct_add(&aw9523b_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&aw9523b_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module aw9523b_module = {
    .name = "aw9523b",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
