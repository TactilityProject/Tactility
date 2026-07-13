// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AW9523B GPIO expander device configuration (set from devicetree).
 *
 * 16 pins total: P0_0..P0_7 map to GpioDescriptor pin numbers 0..7, P1_0..P1_7
 * map to pin numbers 8..15. P0 defaults to open-drain per hardware reset; this
 * driver switches P0 to push-pull mode on start (matches every known board's
 * usage of this chip, since open-drain P0 cannot drive an active-high output
 * without an external pull-up).
 */
struct Aw9523bConfig {
    /** I2C address on the bus (typically 0x58) */
    uint8_t address;
};

#ifdef __cplusplus
}
#endif
