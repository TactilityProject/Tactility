// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_flash_fatfs.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_flash_fatfs, struct Esp32FlashFatfsConfig)

#ifdef __cplusplus
}
#endif
