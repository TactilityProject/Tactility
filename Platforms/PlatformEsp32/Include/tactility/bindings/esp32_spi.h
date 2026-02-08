// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_spi.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_spi, struct Esp32SpiConfig)

#ifdef __cplusplus
}
#endif
