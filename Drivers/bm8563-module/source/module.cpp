// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bm8563_driver;

static const Driver* bm8563_drivers[] = {
    &bm8563_driver,
    nullptr
};

Module bm8563_module = {
    .name = "bm8563",
    .drivers = bm8563_drivers
};

}
