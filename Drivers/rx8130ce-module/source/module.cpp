// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver rx8130ce_driver;

static const Driver* rx8130ce_drivers[] = {
    &rx8130ce_driver,
    nullptr
};

Module rx8130ce_module = {
    .name = "rx8130ce",
    .drivers = rx8130ce_drivers
};

} // extern "C"
