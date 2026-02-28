// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bmi270_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&bmi270_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&bmi270_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol bmi270_module_symbols[];

Module bmi270_module = {
    .name = "bmi270",
    .start = start,
    .stop = stop,
    .symbols = bmi270_module_symbols,
    .internal = nullptr
};

}
