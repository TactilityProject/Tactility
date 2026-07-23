// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver sc2356_driver;

static Driver* const sc2356_drivers[] = {
    &sc2356_driver,
    nullptr
};

Module sc2356_module = {
    .name = "sc2356",
    .drivers = sc2356_drivers
};

} // extern "C"
