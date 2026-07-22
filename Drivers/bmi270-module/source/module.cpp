// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bmi270_driver;

static const Driver* bmi270_drivers[] = {
    &bmi270_driver,
    nullptr
};

extern const ModuleSymbol bmi270_module_symbols[];

Module bmi270_module = {
    .name = "bmi270",
    .drivers = bmi270_drivers,
    .symbols = bmi270_module_symbols
};

}
