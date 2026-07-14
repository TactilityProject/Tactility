// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver es8388_driver;

static error_t start() {
    check(driver_construct_add(&es8388_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&es8388_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol es8388_module_symbols[];

Module es8388_module = {
    .name = "es8388",
    .start = start,
    .stop = stop,
    .symbols = es8388_module_symbols,
    .internal = nullptr
};

}
