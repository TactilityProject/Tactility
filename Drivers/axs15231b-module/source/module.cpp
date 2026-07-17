// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axs15231b_display_driver;
extern Driver axs15231b_touch_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&axs15231b_display_driver) == ERROR_NONE);
    check(driver_construct_add(&axs15231b_touch_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&axs15231b_touch_driver) == ERROR_NONE);
    check(driver_remove_destruct(&axs15231b_display_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module axs15231b_module = {
    .name = "axs15231b",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
