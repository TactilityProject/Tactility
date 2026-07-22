// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver dummy_i2s_amp_driver;

static const Driver* dummy_drivers[] = {
    &dummy_i2s_amp_driver,
    nullptr
};

extern const ModuleSymbol dummy_i2s_amp_module_symbols[];

Module dummy_i2s_amp_module = {
    .name = "dummy_i2s_amp",
    .drivers = dummy_drivers,
    .symbols = dummy_i2s_amp_module_symbols
};

}
