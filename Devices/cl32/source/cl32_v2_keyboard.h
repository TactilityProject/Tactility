#pragma once

#include <tactility/device.h>

// Constructs and starts the tca8418 keyboard on i2c0. Only called for revision 2 hardware -
// revision 3 replaced the keyboard chip with the power-supply chip (see cl32_detect.cpp).
void cl32_create_keyboard(struct Device* i2c0);
