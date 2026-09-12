// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver mpu6886_driver;

static Driver* const mpu6886_drivers[] = {
    &mpu6886_driver,
    nullptr
};

Module mpu6886_module = {
    .name = "mpu6886",
    .drivers = mpu6886_drivers
};

} // extern "C"
