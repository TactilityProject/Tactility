// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver aw9523b_driver;

static Driver* const aw9523b_drivers[] = {
    &aw9523b_driver,
    nullptr
};

Module aw9523b_module = {
    .name = "aw9523b",
    .drivers = aw9523b_drivers
};

}
