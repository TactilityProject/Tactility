// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver pi4ioe5v6408_driver;

static Driver* const pi4ioe5v6408_drivers[] = {
    &pi4ioe5v6408_driver,
    nullptr
};

Module pi4ioe5v6408_module = {
    .name = "pi4ioe5v6408",
    .drivers = pi4ioe5v6408_drivers
};

}
