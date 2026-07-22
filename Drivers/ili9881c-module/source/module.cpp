// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ili9881c_driver;

static Driver* const ili9881c_drivers[] = {
    &ili9881c_driver,
    nullptr
};

Module ili9881c_module = {
    .name = "ili9881c",
    .drivers = ili9881c_drivers
};

} // extern "C"
