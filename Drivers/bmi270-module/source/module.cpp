// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bmi270_driver;

static Driver* const bmi270_drivers[] = {
    &bmi270_driver,
    nullptr
};

Module bmi270_module = {
    .name = "bmi270",
    .start = nullptr,
    .stop = nullptr,
    .drivers = bmi270_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
