// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_wifi_pinned.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_wifi_pinned, struct Esp32WifiPinnedConfig)

#ifdef __cplusplus
}
#endif
