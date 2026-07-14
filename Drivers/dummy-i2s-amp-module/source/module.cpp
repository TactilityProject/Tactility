// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver dummy_i2s_amp_driver;

static error_t start() {
    check(driver_construct_add(&dummy_i2s_amp_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&dummy_i2s_amp_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol dummy_i2s_amp_module_symbols[];

Module dummy_i2s_amp_module = {
    .name = "dummy_i2s_amp",
    .start = start,
    .stop = stop,
    .symbols = dummy_i2s_amp_module_symbols,
    .internal = nullptr
};

}
