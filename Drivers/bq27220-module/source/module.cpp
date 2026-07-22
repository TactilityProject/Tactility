// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bq27220_driver;
extern Driver bq27220_power_supply_driver;

static const Driver* bq27220_drivers[] = {
    &bq27220_driver,
    &bq27220_power_supply_driver,
    nullptr
};

Module bq27220_module = {
    .name = "bq27220",
    .drivers = bq27220_drivers
};

} // extern "C"
