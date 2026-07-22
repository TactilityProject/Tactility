// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver mpu6886_driver;

static Driver* const mpu6886_drivers[] = {
    &mpu6886_driver,
    nullptr
};

extern const ModuleSymbol mpu6886_module_symbols[];

Module mpu6886_module = {
    .name = "mpu6886",
    .drivers = mpu6886_drivers,
    .symbols = mpu6886_module_symbols,
};

} // extern "C"
