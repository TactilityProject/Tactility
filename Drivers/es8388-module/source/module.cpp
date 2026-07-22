// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver es8388_driver;

static const Driver* es8388_drivers[] = {
    &es8388_driver,
    nullptr
};

extern const ModuleSymbol es8388_module_symbols[];

Module es8388_module = {
    .name = "es8388",
    .drivers = es8388_drivers,
    .symbols = es8388_module_symbols
};

}
