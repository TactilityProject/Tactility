// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver aw88298_driver;

static error_t start() {
    check(driver_construct_add(&aw88298_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&aw88298_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol aw88298_module_symbols[];

Module aw88298_module = {
    .name = "aw88298",
    .start = start,
    .stop = stop,
    .symbols = aw88298_module_symbols,
    .internal = nullptr
};

}
