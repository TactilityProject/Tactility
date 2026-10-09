#pragma once

#include <tactility/device.h>

/** @return true if the device was successfully constructed, added and started. */
bool cl32_create_keyboard(struct Device* i2c0);

void cl32_destroy_keyboard();
