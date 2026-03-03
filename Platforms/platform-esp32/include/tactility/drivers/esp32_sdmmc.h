// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gpio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SdmmcConfig {
    struct GpioPinSpec pin_clk;
    struct GpioPinSpec pin_cmd;
    struct GpioPinSpec pin_d0;
    struct GpioPinSpec pin_d1;
    struct GpioPinSpec pin_d2;
    struct GpioPinSpec pin_d3;
    struct GpioPinSpec pin_d4;
    struct GpioPinSpec pin_d5;
    struct GpioPinSpec pin_d6;
    struct GpioPinSpec pin_d7;
    struct GpioPinSpec pin_cd;
    struct GpioPinSpec pin_wp;
    uint8_t bus_width;
    bool wp_active_high;
    bool enable_uhs;
};

#ifdef __cplusplus
}
#endif
