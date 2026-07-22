// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cst328_driver;

static Driver* const cst328_drivers[] = {
    &cst328_driver,
    nullptr
};

Module cst328_module = {
    .name = "cst328",
    .drivers = cst328_drivers
};

} // extern "C"
