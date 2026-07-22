// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tca9534_driver;

static Driver* const tca9534_drivers[] = {
    &tca9534_driver,
    nullptr
};

Module tca9534_module = {
    .name = "tca9534",
    .drivers = tca9534_drivers
};

}
