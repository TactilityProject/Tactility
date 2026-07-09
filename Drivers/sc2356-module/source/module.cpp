// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver sc2356_driver;

static error_t start() {
    check(driver_construct_add(&sc2356_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&sc2356_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol sc2356_module_symbols[];

Module sc2356_module = {
    .name = "sc2356",
    .start = start,
    .stop = stop,
    .symbols = sc2356_module_symbols,
    .internal = nullptr
};

} // extern "C"
