// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** No device-tree configuration required for the NimBLE BLE driver. */
struct Esp32BleNimbleConfig {
    int _unused; /**< Placeholder — driver reads all config from NimBLE Kconfig. */
};

#ifdef __cplusplus
}
#endif
