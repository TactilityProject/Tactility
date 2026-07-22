// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axs5106_driver;

static const Driver* axs5106_drivers[] = {
    &axs5106_driver,
    nullptr
};

Module axs5106_module = {
    .name = "axs5106",
    .drivers = axs5106_drivers
};

} // extern "C"
