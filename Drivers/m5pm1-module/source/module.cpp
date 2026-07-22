// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver m5pm1_driver;
extern Driver m5pm1_power_supply_driver;

static const Driver* m5pm1_drivers[] = {
    &m5pm1_driver,
    &m5pm1_power_supply_driver,
    nullptr
};

extern const ModuleSymbol m5pm1_module_symbols[];

Module m5pm1_module = {
    .name = "m5pm1",
    .drivers = m5pm1_drivers,
    .symbols = m5pm1_module_symbols
};

} // extern "C"
