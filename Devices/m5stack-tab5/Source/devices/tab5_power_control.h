#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>

// Board-specific charge-control/quick-charge/power-off glue for the IP2326 charger IC, driven
// through io_expander1 (PI4IOE5V6408 at 0x44) - separate from ina226's power-supply device
// (battery voltage/capacity, on the same board's INA226). Both are POWER_SUPPLY_TYPE kernel
// devices; consumers (Power.cpp, Statusbar.cpp, PowerOff.cpp) already enumerate every
// POWER_SUPPLY_TYPE device generically, so having two devices split by concern is the intended
// pattern rather than a gap - see module.cpp's create_tab5_power_control().
extern struct Driver tab5_power_control_driver;

#ifdef __cplusplus
}
#endif
