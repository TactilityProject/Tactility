// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "gpio.h"
#include <tactility/error.h>

struct GpioControllerApi {
    /**
     * @brief Sets the logical level of a GPIO pin.
     * @param[in] device the GPIO controller device
     * @param[in] pin the pin index
     * @param[in] high true to set the pin high, false to set it low
     * @return ERROR_NONE if successful
     */
    error_t (*set_level)(struct Device* device, gpio_pin_t pin, bool high);

    /**
     * @brief Gets the logical level of a GPIO pin.
     * @param[in] device the GPIO controller device
     * @param[in] pin the pin index
     * @param[out] high pointer to store the pin level
     * @return ERROR_NONE if successful
     */
    error_t (*get_level)(struct Device* device, gpio_pin_t pin, bool* high);

    /**
     * @brief Configures the options for a GPIO pin.
     * @param[in] device the GPIO controller device
     * @param[in] pin the pin index
     * @param[in] options configuration flags (direction, pull-up/down, etc.)
     * @return ERROR_NONE if successful
     */
    error_t (*set_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t options);

    /**
     * @brief Gets the configuration options for a GPIO pin.
     * @param[in] device the GPIO controller device
     * @param[in] pin the pin index
     * @param[out] options pointer to store the configuration flags
     * @return ERROR_NONE if successful
     */
    error_t (*get_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);

    /**
     * @brief Gets the number of pins supported by the controller.
     * @param[in] device the GPIO controller device
     * @param[out] count pointer to store the number of pins
     * @return ERROR_NONE if successful
     */
    error_t (*get_pin_count)(struct Device* device, uint32_t* count);
};

/**
 * @brief Sets the logical level of a GPIO pin.
 * @param[in] device the GPIO controller device
 * @param[in] pin the pin index
 * @param[in] high true to set the pin high, false to set it low
 * @return ERROR_NONE if successful
 */
error_t gpio_controller_set_level(struct Device* device, gpio_pin_t pin, bool high);

/**
 * @brief Gets the logical level of a GPIO pin.
 * @param[in] device the GPIO controller device
 * @param[in] pin the pin index
 * @param[out] high pointer to store the pin level
 * @return ERROR_NONE if successful
 */
error_t gpio_controller_get_level(struct Device* device, gpio_pin_t pin, bool* high);

/**
 * @brief Configures the options for a GPIO pin.
 * @param[in] device the GPIO controller device
 * @param[in] pin the pin index
 * @param[in] options configuration flags (direction, pull-up/down, etc.)
 * @return ERROR_NONE if successful
 */
error_t gpio_controller_set_options(struct Device* device, gpio_pin_t pin, gpio_flags_t options);

/**
 * @brief Gets the configuration options for a GPIO pin.
 * @param[in] device the GPIO controller device
 * @param[in] pin the pin index
 * @param[out] options pointer to store the configuration flags
 * @return ERROR_NONE if successful
 */
error_t gpio_controller_get_options(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);

/**
     * @brief Gets the number of pins supported by the controller.
     * @param[in] device the GPIO controller device
     * @param[out] count pointer to store the number of pins
     * @return ERROR_NONE if successful
     */
error_t gpio_controller_get_pin_count(struct Device* device, uint32_t* count);

/**
 * @brief Configures the options for a GPIO pin using a pin configuration structure.
 * @param[in] device the GPIO controller device
 * @param[in] config the pin configuration structure
 * @return ERROR_NONE if successful
 */
static inline error_t gpio_set_options_config(struct Device* device, const struct GpioPinConfig* config) {
    return gpio_controller_set_options(device, config->pin, config->flags);
}

extern const struct DeviceType GPIO_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
