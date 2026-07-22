// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cardputer_keyboard_driver;
extern Driver cardputer_adv_keyboard_driver;

static const Driver* m5stack_drivers[] = {
    &cardputer_keyboard_driver,
    &cardputer_adv_keyboard_driver,
    nullptr
};

Module m5stack_module = {
    .name = "m5stack",
    .drivers = m5stack_drivers
};

} // extern "C"
