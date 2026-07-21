// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver es7210_driver;

static error_t start() {
    check(driver_construct_add(&es7210_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&es7210_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol es7210_module_symbols[];

Module es7210_module = {
    .name = "es7210",
    .start = start,
    .stop = stop,
    .symbols = es7210_module_symbols,
    .internal = nullptr
};

}
