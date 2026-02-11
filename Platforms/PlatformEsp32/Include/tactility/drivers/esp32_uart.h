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
    struct GpioPinSpec pin_tx;
    struct GpioPinSpec pin_rx;
    struct GpioPinSpec pin_cts;
    struct GpioPinSpec pin_rts;
};

#ifdef __cplusplus
}
#endif
