// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32UsbHostConfig {
    uint8_t peripheral_map; /**< BIT(0)=controller 0 (HS/UTMI, e.g. USB-A on Tab5), BIT(1)=controller 1 (FS/FSLS, e.g. USB-C OTG on Tab5) */
    bool hid;
    bool midi;
    bool msc;
    bool audio;
};

#ifdef __cplusplus
}
#endif
