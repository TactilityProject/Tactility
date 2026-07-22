// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver qmi8658_driver;

static const Driver* qmi8658_drivers[] = {
    &qmi8658_driver,
    nullptr
};

extern const ModuleSymbol qmi8658_module_symbols[];

Module qmi8658_module = {
    .name = "qmi8658",
    .drivers = qmi8658_drivers,
    .symbols = qmi8658_module_symbols,
};

} // extern "C"
