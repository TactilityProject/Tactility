// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver shtc3_driver;

static Driver* const shtc3_drivers[] = {
    &shtc3_driver,
    nullptr
};

extern const ModuleSymbol shtc3_module_symbols[];

Module shtc3_module = {
    .name = "shtc3",
    .start = nullptr,
    .stop = nullptr,
    .drivers = shtc3_drivers,
    .symbols = shtc3_module_symbols,
    .internal = nullptr
};

} // extern "C"
