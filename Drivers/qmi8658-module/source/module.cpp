// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver qmi8658_driver;

static error_t start() {
    check(driver_construct_add(&qmi8658_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
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
