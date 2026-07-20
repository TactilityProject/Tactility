// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Ch422gConfig {
    /** Address of the WR_SET (system config) pseudo-register. RD_IO and WR_IO are separate
     *  fixed pseudo-addresses (the chip has no address-select pins, so this is always 0x24). */
    uint8_t address;
};

#ifdef __cplusplus
}
#endif
