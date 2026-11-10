// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gpio_power_off_driver;

static Driver* const gpio_power_off_drivers[] = {
    &gpio_power_off_driver,
    nullptr
};

Module gpio_poweroff_module = {
    .name = "gpio_power_off",
    .start = nullptr,
    .stop = nullptr,
    .drivers = gpio_power_off_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
