// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tps65185_driver;

static Driver* const tps65185_drivers[] = {
    &tps65185_driver,
    nullptr
};

Module tps65185_module = {
    .name = "tps65185",
    .start = nullptr,
    .stop = nullptr,
    .drivers = tps65185_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
