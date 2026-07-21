// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Devicetree configuration for a PWM device.
 */
struct PwmConfig {
    /** The PWM period, in nanoseconds */
    uint32_t period_ns;
    /** The PWM duty cycle (active time within one period), in nanoseconds */
    uint32_t duty_ns;
    /** Whether the output polarity is inverted */
    bool inverted;
};

/**
 * @brief API for PWM drivers.
 */
struct PwmApi {
    /**
     * @brief Sets the PWM period.
     * @param[in] device the PWM device
     * @param[in] period_ns the period, in nanoseconds
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_period)(struct Device* device, uint32_t period_ns);

    /**
     * @brief Gets the PWM period.
     * @param[in] device the PWM device
     * @param[out] period_ns the period, in nanoseconds
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_period)(struct Device* device, uint32_t* period_ns);

    /**
     * @brief Sets the PWM duty cycle.
     * @param[in] device the PWM device
     * @param[in] duty_ns the active time within one period, in nanoseconds
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_duty)(struct Device* device, uint32_t duty_ns);

    /**
     * @brief Gets the PWM duty cycle.
     * @param[in] device the PWM device
     * @param[out] duty_ns the active time within one period, in nanoseconds
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_duty)(struct Device* device, uint32_t* duty_ns);

    /**
     * @brief Sets whether the output polarity is inverted.
     * @param[in] device the PWM device
     * @param[in] inverted true to invert the output polarity
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_inverted)(struct Device* device, bool inverted);

    /**
     * @brief Gets whether the output polarity is inverted.
     * @param[in] device the PWM device
     * @param[out] inverted true when the output polarity is inverted
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*is_inverted)(struct Device* device, bool* inverted);

    /**
     * @brief Enables the PWM output.
     * @param[in] device the PWM device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*enable)(struct Device* device);

    /**
     * @brief Disables the PWM output.
     * @param[in] device the PWM device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*disable)(struct Device* device);

    /**
     * @brief Gets whether the PWM output is enabled.
     * @param[in] device the PWM device
     * @param[out] enabled true when the output is enabled
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*is_enabled)(struct Device* device, bool* enabled);
};

/**
 * @brief Sets the PWM period using the specified PWM device.
 */
error_t pwm_set_period(struct Device* device, uint32_t period_ns);

/**
 * @brief Gets the PWM period using the specified PWM device.
 */
error_t pwm_get_period(struct Device* device, uint32_t* period_ns);

/**
 * @brief Sets the PWM duty cycle using the specified PWM device.
 */
error_t pwm_set_duty(struct Device* device, uint32_t duty_ns);

/**
 * @brief Gets the PWM duty cycle using the specified PWM device.
 */
error_t pwm_get_duty(struct Device* device, uint32_t* duty_ns);

/**
 * @brief Sets whether the output polarity is inverted using the specified PWM device.
 */
error_t pwm_set_inverted(struct Device* device, bool inverted);

/**
 * @brief Gets whether the output polarity is inverted using the specified PWM device.
 */
error_t pwm_is_inverted(struct Device* device, bool* inverted);

/**
 * @brief Enables the PWM output using the specified PWM device.
 */
error_t pwm_enable(struct Device* device);

/**
 * @brief Disables the PWM output using the specified PWM device.
 */
error_t pwm_disable(struct Device* device);

/**
 * @brief Gets whether the PWM output is enabled using the specified PWM device.
 */
error_t pwm_is_enabled(struct Device* device, bool* enabled);

extern const struct DeviceType PWM_TYPE;

#ifdef __cplusplus
}
#endif
