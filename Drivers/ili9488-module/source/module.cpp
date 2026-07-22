// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ili9488_driver;

static Driver* const ili9488_drivers[] = {
    &ili9488_driver,
    nullptr
};

Module ili9488_module = {
    .name = "ili9488",
    .drivers = ili9488_drivers
};

} // extern "C"
