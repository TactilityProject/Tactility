// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7701_driver;

static const Driver* st7701_drivers[] = {
    &st7701_driver,
    nullptr
};

Module st7701_module = {
    .name = "st7701",
    .drivers = st7701_drivers
};

} // extern "C"
