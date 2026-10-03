// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver esp32_led_strip_driver;

static Driver* const esp32_led_strip_drivers[] = {
    &esp32_led_strip_driver,
    nullptr
};

Module esp32_led_strip_module = {
    .name = "esp32_led_strip",
    .start = nullptr,
    .stop = nullptr,
    .drivers = esp32_led_strip_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
