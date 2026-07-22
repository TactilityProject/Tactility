// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver aw88298_driver;

extern const ModuleSymbol aw88298_module_symbols[];

static const Driver* aw88298_drivers[] = {
    &aw88298_driver,
    nullptr
};

Module aw88298_module = {
    .name = "aw88298",
    .drivers = aw88298_drivers,
    .symbols = aw88298_module_symbols
};

}
