// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ina226_driver;
extern Driver ina226_power_supply_driver;

static const Driver* ina226_drivers[] = {
    &ina226_driver,
    &ina226_power_supply_driver,
    nullptr
};

extern const ModuleSymbol ina226_module_symbols[];

Module ina226_module = {
    .name = "ina226",
    .drivers = ina226_drivers,
    .symbols = ina226_module_symbols,
};

} // extern "C"
