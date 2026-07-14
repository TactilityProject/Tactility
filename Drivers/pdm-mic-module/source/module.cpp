// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver pdm_mic_driver;

static error_t start() {
    check(driver_construct_add(&pdm_mic_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&pdm_mic_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol pdm_mic_module_symbols[];

Module pdm_mic_module = {
    .name = "pdm_mic",
    .start = start,
    .stop = stop,
    .symbols = pdm_mic_module_symbols,
    .internal = nullptr
};

}
