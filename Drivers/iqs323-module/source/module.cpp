// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver iqs323_driver;

static Driver* const iqs323_drivers[] = {
    &iqs323_driver,
    nullptr
};

Module iqs323_module = {
    .name = "iqs323",
    .start = nullptr,
    .stop = nullptr,
    .drivers = iqs323_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
