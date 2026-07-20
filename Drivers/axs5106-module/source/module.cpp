// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axs5106_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&axs5106_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&axs5106_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module axs5106_module = {
    .name = "axs5106",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
