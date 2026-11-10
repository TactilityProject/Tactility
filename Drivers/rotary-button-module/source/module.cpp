// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver rotary_button_driver;

static Driver* const rotary_button_drivers[] = {
    &rotary_button_driver,
    nullptr
};

Module rotary_button_module = {
    .name = "rotary-button",
    .start = nullptr,
    .stop = nullptr,
    .drivers = rotary_button_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
