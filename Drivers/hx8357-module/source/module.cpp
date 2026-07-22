// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern Driver hx8357_driver;

static const Driver* hx8357_drivers[] = {
    &hx8357_driver,
    nullptr
};

extern "C" {

Module hx8357_module = {
    .name = "hx8357",
    .drivers = hx8357_drivers
};

} // extern "C"
