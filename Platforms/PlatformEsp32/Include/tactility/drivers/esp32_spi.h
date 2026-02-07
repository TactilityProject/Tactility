// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/spi_common.h>
#include <tactility/drivers/spi_controller.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SpiConfig {
    spi_host_device_t host;
    /** Data 0 pin */
    int pin_mosi;
    /** Data 1 pin */
    int pin_miso;
    /** Clock pin */
    int pin_sclk;
    /** Data 2 pin */
    int pin_wp;
    /** Data 3 pin */
    int pin_hd;
    /** Data transfer size limit in bytes. 0 means the platform decides the limit. */
    int max_transfer_size;
};

#ifdef __cplusplus
}
#endif
