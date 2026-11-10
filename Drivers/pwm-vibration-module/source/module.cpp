// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver pwm_vibration_driver;

static Driver* const pwm_vibration_drivers[] = {
    &pwm_vibration_driver,
    nullptr
};

Module pwm_vibration_module = {
    .name = "pwm_vibration",
    .start = nullptr,
    .stop = nullptr,
    .drivers = pwm_vibration_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
