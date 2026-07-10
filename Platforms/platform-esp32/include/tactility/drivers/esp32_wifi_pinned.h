// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** No device-tree configuration required for the ESP32 pinned WiFi driver. */
struct Esp32WifiPinnedConfig {
    int _unused; /**< Placeholder — driver has no configurable properties. */
};

#ifdef __cplusplus
}
#endif
