#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/driver.h>

struct Cl32V4KeyboardConfig {
    uint8_t address;
};

extern struct Driver cl32_v4_keyboard_driver;

#ifdef __cplusplus
}
#endif
