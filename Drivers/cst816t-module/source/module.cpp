// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cst816t_driver;

static error_t start() {
    return driver_construct_add(&cst816t_driver);
}

static error_t stop() {
    return driver_remove_destruct(&cst816t_driver);
}

Module cst816t_module = {
    .name = "cst816t",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
