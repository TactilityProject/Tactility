// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver pi4ioe5v6408_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&pi4ioe5v6408_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&pi4ioe5v6408_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module pi4ioe5v6408_module = {
    .name = "pi4ioe5v6408",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
