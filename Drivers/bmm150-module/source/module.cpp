// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bmm150_driver;

static Driver* const bmm150_drivers[] = {
    &bmm150_driver,
    nullptr
};

Module bmm150_module = {
    .name = "bmm150",
    .start = nullptr,
    .stop = nullptr,
    .drivers = bmm150_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
