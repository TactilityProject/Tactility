#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/driver.h>

struct Cl32V4PowerConfig {
    uint8_t address;
};

extern struct Driver cl32_v4_power_driver;
extern struct Driver cl32_v4_power_supply_driver;

#ifdef __cplusplus
}
#endif
