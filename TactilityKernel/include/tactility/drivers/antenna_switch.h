// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

struct AntennaSwitchApi {
    /** Select an antenna by its zero-based index. */
    error_t (*set_antenna)(struct Device* device, uint32_t antenna);
    /** Read the currently selected antenna index. */
    error_t (*get_antenna)(struct Device* device, uint32_t* antenna);
};

/** @retval ERROR_OUT_OF_RANGE when the index cannot be represented by the switch */
error_t antenna_switch_set_antenna(struct Device* device, uint32_t antenna);

/** @param[out] antenna receives the currently selected zero-based antenna index */
error_t antenna_switch_get_antenna(struct Device* device, uint32_t* antenna);

extern const struct DeviceType ANTENNA_SWITCH_TYPE;

#ifdef __cplusplus
}
#endif
