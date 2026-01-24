// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Tactility/bindings/bindings.h>
#include <Tactility/drivers/Esp32I2c.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_i2c, struct Esp32I2cConfig)

#ifdef __cplusplus
}
#endif
