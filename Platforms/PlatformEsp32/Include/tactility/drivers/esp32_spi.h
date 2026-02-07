// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/spi_common.h>
#include <tactility/drivers/spi_controller.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SpiConfig {
    spi_host_device_t host;
    int pin_mosi;
    int pin_miso;
    int pin_sclk;
    int pin_wp;
    int pin_hd;
    int max_transfer_sz;
};

#ifdef __cplusplus
}
#endif
