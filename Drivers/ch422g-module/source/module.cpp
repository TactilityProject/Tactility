// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ch422g_driver;

static const Driver* ch422g_drivers[] = {
    &ch422g_driver,
    nullptr
};

Module ch422g_module = {
    .name = "ch422g",
    .drivers = ch422g_drivers
};

}
