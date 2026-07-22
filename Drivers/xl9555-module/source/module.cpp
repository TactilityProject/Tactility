// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver xl9555_driver;

static const Driver* xl9555_drivers[] = {
    &xl9555_driver,
    nullptr
};

Module xl9555_module = {
    .name = "xl9555",
    .drivers = xl9555_drivers
};

}
