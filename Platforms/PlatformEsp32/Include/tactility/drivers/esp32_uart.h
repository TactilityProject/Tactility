// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/uart.h>
#include <tactility/drivers/uart_controller.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32UartConfig {
    uart_port_t port;
    int pinTx;
    int pinRx;
    int pinCts;
    int pinRts;
};

#ifdef __cplusplus
}
#endif
