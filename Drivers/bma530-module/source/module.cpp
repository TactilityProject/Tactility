// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bma530_driver;

static Driver* const bma530_drivers[] = {
    &bma530_driver,
    nullptr
};

Module bma530_module = {
    .name = "bma530",
    .start = nullptr,
    .stop = nullptr,
    .drivers = bma530_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
