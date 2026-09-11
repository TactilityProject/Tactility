// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver qmi8658_driver;

static Driver* const qmi8658_drivers[] = {
    &qmi8658_driver,
    nullptr
};

Module qmi8658_module = {
    .name = "qmi8658",
    .drivers = qmi8658_drivers
};

} // extern "C"
