// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver max17048_driver;
extern Driver max17048_power_supply_driver;

static Driver* const max17048_drivers[] = {
    &max17048_driver,
    &max17048_power_supply_driver,
    nullptr
};

Module max17048_module = {
    .name = "max17048",
    .start = nullptr,
    .stop = nullptr,
    .drivers = max17048_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
