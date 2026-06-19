// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/uart.h>
#include <hal/i2c_types.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/grove.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32GroveConfig {
    enum GroveMode defaultMode;
    struct GpioPinSpec pinSdaTx;
    struct GpioPinSpec pinSclRx;
    uart_port_t uartPort;
    i2c_port_t i2cPort;
    uint32_t i2cClockFrequency;
};

#ifdef __cplusplus
}
#endif
