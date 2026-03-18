// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver mpu6886_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&mpu6886_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&mpu6886_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol mpu6886_module_symbols[];

Module mpu6886_module = {
    .name = "mpu6886",
    .start = start,
    .stop = stop,
    .symbols = mpu6886_module_symbols,
    .internal = nullptr
};

} // extern "C"
