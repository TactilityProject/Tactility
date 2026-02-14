// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_uart.h>
#include <driver/uart.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_uart, struct Esp32UartConfig)

#ifdef __cplusplus
}
#endif
