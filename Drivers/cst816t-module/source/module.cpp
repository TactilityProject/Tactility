// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cst816t_driver;

static Driver* const cst816t_drivers[] = {
    &cst816t_driver,
    nullptr
};

Module cst816t_module = {
    .name = "cst816t",
    .start = nullptr,
    .stop = nullptr,
    .drivers = cst816t_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
