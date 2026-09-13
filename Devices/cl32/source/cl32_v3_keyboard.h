#pragma once

#include <tactility/device.h>

// Constructs and starts the tca8418 keyboard on i2c0. Only called for revision 3 hardware - same
// chip/wiring as cl32_v2_keyboard.cpp's revision 2 keyboard, different physical key layout (see
// CL-32/CL-32's CL32_keyboard.cpp).
void cl32_v3_create_keyboard(struct Device* i2c0);
