// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver m5pm1_driver;

static error_t start() {
    check(driver_construct_add(&m5pm1_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&m5pm1_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol m5pm1_module_symbols[];

Module m5pm1_module = {
    .name = "m5pm1",
    .start = start,
    .stop = stop,
    .symbols = m5pm1_module_symbols,
    .internal = nullptr
};

} // extern "C"
