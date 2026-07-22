// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver rgb_display_driver;

static const Driver* rgbd_display_drivers[] = {
    &rgb_display_driver,
    nullptr
};

Module rgb_display_module = {
    .name = "rgb_display",
    .drivers = rgbd_display_drivers
};

} // extern "C"
