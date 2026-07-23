// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cst66xx_driver;

static Driver* const cst66xx_drivers[] = {
    &cst66xx_driver,
    nullptr
};

Module cst66xx_module = {
    .name = "cst66xx",
    .drivers = cst66xx_drivers
};

} // extern "C"
