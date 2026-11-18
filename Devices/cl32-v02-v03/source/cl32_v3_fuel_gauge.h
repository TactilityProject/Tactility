#pragma once

#include <tactility/device.h>

/** Creates the MAX17048 fuel gauge on i2c0. It only exists on revision 3 hardware. */
bool cl32_v3_create_fuel_gauge(Device* i2c0);

void cl32_v3_destroy_fuel_gauge();
