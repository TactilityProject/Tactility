// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bm8563_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&bm8563_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&bm8563_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol bm8563_module_symbols[];

Module bm8563_module = {
    .name = "bm8563",
    .start = start,
    .stop = stop,
    .symbols = bm8563_module_symbols,
    .internal = nullptr
};

}
