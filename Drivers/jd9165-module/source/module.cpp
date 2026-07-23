// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver jd9165_driver;

static Driver* const jd9165_drivers[] = {
    &jd9165_driver,
    nullptr
};

Module jd9165_module = {
    .name = "jd9165",
    .drivers = jd9165_drivers
};

} // extern "C"
