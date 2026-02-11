// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/uart.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32UartConfig {
    uart_port_t port;
    struct GpioPinSpec pinTx;
    struct GpioPinSpec pinRx;
    struct GpioPinSpec pinCts;
    struct GpioPinSpec pinRts;
};

#ifdef __cplusplus
}
#endif
