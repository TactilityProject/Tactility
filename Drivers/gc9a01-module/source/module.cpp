// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gc9a01_driver;

static Driver* const gc9a01_drivers[] = {
    &gc9a01_driver,
    nullptr
};

Module gc9a01_module = {
    .name = "gc9a01",
    .drivers = gc9a01_drivers
};

} // extern "C"
