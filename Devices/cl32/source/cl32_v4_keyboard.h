#pragma once

#include <tactility/device.h>
#include <tactility/driver.h>

extern struct Driver cl32_v4_keyboard_driver;

// Constructs and starts the v4 core chip's keyboard function on i2c0. Only called for revision 4
// hardware, once the core chip has been probed at CL32_V4_CORE_I2C_ADDRESS (see cl32_detect.cpp).
void cl32_v4_create_keyboard(struct Device* i2c0);
