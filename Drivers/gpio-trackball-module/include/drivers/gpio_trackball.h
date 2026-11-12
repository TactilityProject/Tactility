// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>
#include <tactility/drivers/trackball.h>
#include <tactility/error.h>
#include <stdbool.h>
#include <stdint.h>

struct Device;

struct GpioTrackballConfig {
    struct GpioPinSpec pin_right;
    struct GpioPinSpec pin_up;
    struct GpioPinSpec pin_left;
    struct GpioPinSpec pin_down;
    struct GpioPinSpec pin_click;
    enum TrackballMode default_mode;
};

#ifdef __cplusplus
}
#endif
