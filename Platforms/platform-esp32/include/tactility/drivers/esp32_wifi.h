// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** No device-tree configuration required for the ESP32 WiFi driver. */
struct Esp32WifiConfig {
    int _unused; /**< Placeholder — driver reads all config from Kconfig. */
};

#ifdef __cplusplus
}
#endif
