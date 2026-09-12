// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ch422g_driver;

static Driver* const ch422g_drivers[] = {
    &ch422g_driver,
    nullptr
};

Module ch422g_module = {
    .name = "ch422g",
    .start = nullptr,
    .stop = nullptr,
    .drivers = ch422g_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
