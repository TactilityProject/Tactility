// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver pcf85063_driver;

static Driver* const pcf85063_drivers[] = {
    &pcf85063_driver,
    nullptr
};

Module pcf85063_module = {
    .name = "pcf85063",
    .start = nullptr,
    .stop = nullptr,
    .drivers = pcf85063_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
