#pragma once

#include <tactility/device.h>
#include <tactility/driver.h>

extern struct Driver cl32_v4_keyboard_driver;

/** @return true if the device was successfully constructed, added and started. */
bool cl32_v4_create_keyboard(struct Device* i2c0);

void cl32_v4_destroy_keyboard();
