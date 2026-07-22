// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ili9341_driver;

static const Driver* ili9341_drivers[] = {
    &ili9341_driver,
    nullptr
};

Module ili9341_module = {
    .name = "ili9341",
    .drivers = ili9341_drivers
};

} // extern "C"
