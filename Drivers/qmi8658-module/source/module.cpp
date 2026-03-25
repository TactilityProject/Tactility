// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver qmi8658_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&qmi8658_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&qmi8658_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol qmi8658_module_symbols[];

Module qmi8658_module = {
    .name = "qmi8658",
    .start = start,
    .stop = stop,
    .symbols = qmi8658_module_symbols,
    .internal = nullptr
};

} // extern "C"
