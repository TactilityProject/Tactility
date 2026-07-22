// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ft5x06_driver;

static const Driver* ft5x06_drivers[] = {
    &ft5x06_driver,
    nullptr
};

Module ft5x06_module = {
    .name = "ft5x06",
    .drivers = ft5x06_drivers
};

} // extern "C"
