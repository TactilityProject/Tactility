// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver st7123_driver;
extern Driver st7123_touch_driver;

static const Driver* st7123_drivers[] = {
    &st7123_driver,
    &st7123_touch_driver,
    nullptr
};

Module st7123_module = {
    .name = "st7123",
    .drivers = st7123_drivers
};

} // extern "C"
