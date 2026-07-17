// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bq27220_driver;
extern Driver bq27220_power_supply_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&bq27220_power_supply_driver) == ERROR_NONE);
    check(driver_construct_add(&bq27220_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&bq27220_driver) == ERROR_NONE);
    check(driver_remove_destruct(&bq27220_power_supply_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module bq27220_module = {
    .name = "bq27220",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
