// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver ft6x36_driver;

static const Driver* ft6x36_drivers[] = {
    &ft6x36_driver,
    nullptr
};

Module ft6x36_module = {
    .name = "ft6x36",
    .drivers = ft6x36_drivers
};

} // extern "C"
