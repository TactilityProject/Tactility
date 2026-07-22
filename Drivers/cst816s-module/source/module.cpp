// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cst816s_driver;

static Driver* const cst816s_drivers[] = {
    &cst816s_driver,
    nullptr
};

Module cst816s_module = {
    .name = "cst816s",
    .drivers = cst816s_drivers
};

} // extern "C"
