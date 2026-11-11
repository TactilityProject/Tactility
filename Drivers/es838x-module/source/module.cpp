// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver es8388_driver;
extern Driver es8389_driver;

static Driver* const es838x_drivers[] = {
    &es8388_driver,
    &es8389_driver,
    nullptr
};

extern const ModuleSymbol es838x_module_symbols[];

Module es838x_module = {
    .name = "es838x",
    .start = nullptr,
    .stop = nullptr,
    .drivers = es838x_drivers,
    .symbols = es838x_module_symbols,
    .internal = nullptr
};

}
