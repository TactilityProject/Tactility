// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32UsbHostConfig {
    uint8_t peripheral_map; /**< BIT(0)=controller 0 (HS/UTMI, e.g. USB-A on Tab5), BIT(1)=controller 1 (FS/FSLS, e.g. USB-C OTG on Tab5) */
};

struct Esp32UsbHostChildConfig {
    int _unused;
};

#ifdef __cplusplus
}
#endif
