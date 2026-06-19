// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Grove Port modes
 */
enum GroveMode {
    GROVE_MODE_DISABLED = 0,
    GROVE_MODE_UART = 1,
    GROVE_MODE_I2C = 2
};

/**
 * @brief API for Grove drivers.
 *
 * The grove driver is meant for external interfaces with two wires that can be used as UART, I2C or GPIO as needed.
 * It can be used with the Grove connector, but also with others such as Stemma QT.
 */
struct GroveApi {
    /**
     * @brief Sets the Grove port mode.
     * @param[in] device the Grove device
     * @param[in] mode the new mode
     * @return ERROR_NONE on success
     */
    error_t (*set_mode)(struct Device* device, enum GroveMode mode);

    /**
     * @brief Gets the current Grove port mode.
     * @param[in] device the Grove device
     * @param[out] mode pointer to store the current mode
     * @return ERROR_NONE on success
     */
    error_t (*get_mode)(struct Device* device, enum GroveMode* mode);
};

/**
 * @brief Sets the Grove port mode using the specified device.
 */
error_t grove_set_mode(struct Device* device, enum GroveMode mode);

/**
 * @brief Gets the current Grove port mode using the specified device.
 */
error_t grove_get_mode(struct Device* device, enum GroveMode* mode);

extern const struct DeviceType GROVE_TYPE;

#ifdef __cplusplus
}
#endif
