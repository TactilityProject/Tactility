#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/driver.h>

struct Cl32V4LightConfig {
    uint8_t address;
    uint8_t brightness_register;
    uint8_t timeout_register;
    uint8_t brightness_default;
    uint8_t timeout_seconds;
};

extern struct Driver cl32_v4_light_driver;

#ifdef __cplusplus
}
#endif
