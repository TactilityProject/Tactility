// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tca8418_driver;

static const Driver* tca8418_drivers[] = {
    &tca8418_driver,
    nullptr
};

Module tca8418_module = {
    .name = "tca8418",
    .drivers = tca8418_drivers
};

} // extern "C"
