// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief API for vibration motor drivers (ERM/LRA driven through a PWM or GPIO).
 *
 * Strength is a percentage in the range 0..100. 0 means off.
 */
struct VibrationApi {
    /**
     * @brief Sets a sustained vibration strength.
     * @param[in] device the vibration device
     * @param[in] percent 0..100
     * @retval ERROR_NONE on success
     */
    error_t (*set_strength)(struct Device* device, float percent);

    /**
     * @brief Runs the motor at the given strength for a limited time, then stops it.
     * Returns immediately. The pulse is handled by the driver.
     * @param[in] device the vibration device
     * @param[in] duration_ms pulse length in milliseconds
     * @param[in] percent 0..100
     * @retval ERROR_NONE on success
     */
    error_t (*pulse)(struct Device* device, uint32_t duration_ms, float percent);
};

/** See VibrationApi::set_strength */
error_t vibration_set_strength(struct Device* device, float percent);

/** See VibrationApi::pulse */
error_t vibration_pulse(struct Device* device, uint32_t duration_ms, float percent);

extern const struct DeviceType VIBRATION_TYPE;

#ifdef __cplusplus
}
#endif
