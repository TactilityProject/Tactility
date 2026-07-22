// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver xpt2046_softspi_driver;
extern Driver xpt2046_softspi_power_supply_driver;

static const Driver* xpt2046_softspi_drivers[] = {
    &xpt2046_softspi_driver,
    &xpt2046_softspi_power_supply_driver,
    nullptr
};

Module xpt2046_softspi_module = {
    .name = "xpt2046_softspi",
    .drivers = xpt2046_softspi_drivers
};

} // extern "C"
