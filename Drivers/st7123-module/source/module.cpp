// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7123_driver;
extern Driver st7123_touch_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&st7123_driver) == ERROR_NONE);
    check(driver_construct_add(&st7123_touch_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&st7123_touch_driver) == ERROR_NONE);
    check(driver_remove_destruct(&st7123_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module st7123_module = {
    .name = "st7123",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
