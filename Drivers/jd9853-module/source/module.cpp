// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver jd9853_driver;

static const Driver* jd9853_drivers[] = {
    &jd9853_driver,
    nullptr
};

Module jd9853_module = {
    .name = "jd9853",
    .drivers = jd9853_drivers
};

} // extern "C"
