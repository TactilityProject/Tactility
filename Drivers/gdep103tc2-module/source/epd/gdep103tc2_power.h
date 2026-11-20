// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_gpio.h"

#include <drivers/gdep103tc2.h>

#include <tactility/error.h>

struct Gdep103tc2Power {
    GpioDescriptor* enable = nullptr;
    GpioDescriptor* oe = nullptr;
    GpioDescriptor* gmod = nullptr;
    bool on = false;
};

/** Allocates the descriptors and leaves the panel powered down */
error_t gdep103tc2_power_init(Gdep103tc2Power* power, const Gdep103tc2Config* config);

/** Powers down and releases every descriptor */
void gdep103tc2_power_deinit(Gdep103tc2Power* power);

/** Asserts the board enable (if present ofc), then the source and gate driver enables.. */
error_t gdep103tc2_power_on(Gdep103tc2Power* power);

/** Deasserts the source and gate driver enables, then the board enable */
void gdep103tc2_power_off(Gdep103tc2Power* power);
