// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_i2s.h>
#include <driver/i2s_common.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_i2s, struct Esp32I2sConfig)

#ifdef __cplusplus
}
#endif
