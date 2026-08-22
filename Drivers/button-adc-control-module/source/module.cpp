// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver button_adc_control_driver;

static Driver* const button_adc_control_drivers[] = {
    &button_adc_control_driver,
    nullptr
};

Module button_adc_control_module = {
    .name = "button-adc-control",
    .drivers = button_adc_control_drivers
};

} // extern "C"
