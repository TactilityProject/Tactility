// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7796_driver;

static const Driver* st7796_drivers[] = {
    &st7796_driver,
    nullptr
};

Module st7796_module = {
    .name = "st7796",
    .drivers = st7796_drivers
};

} // extern "C"
