// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver m5pm1_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&m5pm1_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
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
