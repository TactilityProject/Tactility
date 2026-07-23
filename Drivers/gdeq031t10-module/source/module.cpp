// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern Driver gdeq031t10_driver;

extern "C" {

static Driver* const gdeq031t10_drivers[] = {
    &gdeq031t10_driver,
    nullptr
};

Module gdeq031t10_module = {
    .name = "gdeq031t10",
    .drivers = gdeq031t10_drivers
};

} // extern "C"
