// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7796_i8080_driver;

static Driver* const st7796_i8080_drivers[] = {
    &st7796_i8080_driver,
    nullptr
};

Module st7796_i8080_module = {
    .name = "st7796_i8080",
    .drivers = st7796_i8080_drivers
};

} // extern "C"
