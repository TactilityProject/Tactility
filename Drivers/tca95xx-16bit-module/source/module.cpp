// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tca95xx_driver;

static Driver* const tca95xx_drivers[] = {
    &tca95xx_driver,
    nullptr
};

Module tca95xx_16bit_module = {
    .name = "tca95xx",
    .drivers = tca95xx_drivers
};

}
