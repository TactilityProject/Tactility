// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver py32ioexpander_driver;

static Driver* const py32ioexpander_drivers[] = {
    &py32ioexpander_driver,
    nullptr
};

Module py32ioexpander_module = {
    .name = "py32ioexpander",
    .drivers = py32ioexpander_drivers
};

} // extern "C"
