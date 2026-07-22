// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver hal_device_driver;

static const Driver* hal_device_drivers[] = {
    &hal_device_driver,
    nullptr
};

Module hal_device_module = {
    .name = "hal-device",
    .drivers = hal_device_drivers
};

}
