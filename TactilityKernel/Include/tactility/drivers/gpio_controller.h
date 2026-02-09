// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "gpio.h"
#include <tactility/error.h>

struct Device;

struct GpioControllerApi {
    struct GpioDescriptor* (*acquire_descriptor)(
        struct Device* controller,
        gpio_pin_t pin_number,
        enum GpioOwnerType owner,
        void* owner_context
    );

    error_t (*release_descriptor)(struct GpioDescriptor* descriptor);

    /**
     * @brief Gets the pin number associated with a descriptor.
     * @param[in] descriptor the pin descriptor
     * @param[out] pin pointer to store the pin number
     * @return ERROR_NONE if successful
     */
    error_t (*get_pin_number)(struct GpioDescriptor* descriptor, gpio_pin_t* pin);

    /**
     * @brief Sets the logical level of a GPIO pin.
     * @param[in,out] descriptor the pin descriptor
     * @param[in] high true to set the pin high, false to set it low
     * @return ERROR_NONE if successful
     */
    error_t (*set_level)(struct GpioDescriptor* descriptor, bool high);

    /**
     * @brief Gets the logical level of a GPIO pin.
     * @param[in] descriptor the pin descriptor
     * @param[out] high pointer to store the pin level
     * @return ERROR_NONE if successful
     */
    error_t (*get_level)(struct GpioDescriptor* descriptor, bool* high);

    /**
     * @brief Configures the options for a GPIO pin.
     * @param[in,out] descriptor the pin descriptor
     * @param[in] options configuration flags (direction, pull-up/down, etc.)
     * @return ERROR_NONE if successful
     */
    error_t (*set_options)(struct GpioDescriptor* descriptor, gpio_flags_t options);

    /**
     * @brief Gets the configuration options for a GPIO pin.
     * @param[in] descriptor the pin descriptor
     * @param[out] options pointer to store the configuration flags
     * @return ERROR_NONE if successful
     */
    error_t (*get_options)(struct GpioDescriptor* descriptor, gpio_flags_t* options);
};

struct GpioDescriptor* acquire_descriptor(
    struct Device* controller,
    gpio_pin_t pin_number,
    enum GpioOwnerType owner,
    void* owner_context
);

error_t release_descriptor(struct GpioDescriptor* descriptor);

/**
 * @brief Gets the number of pins supported by the controller.
 * @param[in] device the GPIO controller device
 * @param[out] count pointer to store the number of pins
 * @return ERROR_NONE if successful
 */
error_t gpio_controller_get_pin_count(struct Device* device, uint32_t* count);

/**
 * @brief Gets the pin number associated with a descriptor.
 * @param[in] descriptor the pin descriptor
 * @param[out] pin pointer to store the pin number
 * @return ERROR_NONE if successful
 */
error_t gpio_descriptor_pin_number(struct GpioDescriptor* descriptor, gpio_pin_t* pin);

/**
 * @brief Sets the logical level of a GPIO pin.
 * @param[in] descriptor the pin descriptor
 * @param[in] high true to set the pin high, false to set it low
 * @return ERROR_NONE if successful
 */
error_t gpio_descriptor_set_level(struct GpioDescriptor* descriptor, bool high);

/**
 * @brief Gets the logical level of a GPIO pin.
 * @param[in] descriptor the pin descriptor
 * @param[out] high pointer to store the pin level
 * @return ERROR_NONE if successful
 */
error_t gpio_descriptor_get_level(struct GpioDescriptor* descriptor, bool* high);

/**
 * @brief Configures the options for a GPIO pin.
 * @param[in] descriptor the pin descriptor
 * @param[in] options configuration flags (direction, pull-up/down, etc.)
 * @return ERROR_NONE if successful
 */
error_t gpio_descriptor_set_options(struct GpioDescriptor* descriptor, gpio_flags_t options);

/**
 * @brief Gets the configuration options for a GPIO pin.
 * @param[in] descriptor the pin descriptor
 * @param[out] options pointer to store the configuration flags
 * @return ERROR_NONE if successful
 */
error_t gpio_descriptor_get_options(struct GpioDescriptor* descriptor, gpio_flags_t* options);

/**
 * @brief Initializes GPIO descriptors for a controller.
 * @param[in,out] device the GPIO controller device
 * @param[in] owner_data pointer to store in the descriptor's owner_data field
 * @return ERROR_NONE if successful
 */
error_t gpio_controller_init_descriptors(struct Device* device, uint32_t pin_count, void* owner_data);

/**
 * @brief Deinitializes GPIO descriptors for a controller.
 * @param[in,out] device the GPIO controller device
 * @return ERROR_NONE if successful
 */
error_t gpio_controller_deinit_descriptors(struct Device* device);

extern const struct DeviceType GPIO_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
