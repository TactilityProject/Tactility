// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver button_control_driver;

static Driver* const button_control_drivers[] = {
    &button_control_driver,
    nullptr
};

Module button_control_module = {
    .name = "button-control",
    .start = nullptr,
    .stop = nullptr,
    .drivers = button_control_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
