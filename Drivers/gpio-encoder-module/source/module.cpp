// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver gpio_encoder_driver;

static Driver* const gpio_encoder_drivers[] = {
    &gpio_encoder_driver,
    nullptr
};

Module gpio_encoder_module = {
    .name = "gpio-encoder",
    .start = nullptr,
    .stop = nullptr,
    .drivers = gpio_encoder_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
