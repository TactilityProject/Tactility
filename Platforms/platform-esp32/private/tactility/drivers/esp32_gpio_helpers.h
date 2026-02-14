#pragma once

#include <tactility/drivers/gpio_descriptor.h>

#include <driver/gpio.h>

/**
 * Releases the given pin descriptor and sets the pointer value to NULL.
 * If the descriptor pointer is null, do nothing.
 */
void release_pin(GpioDescriptor** gpio_descriptor);

/**
 * Acquires the pin descriptor for the given pin spec.
 * If the pin spec is invalid, the pointer is set to null.
 * @return true if the pin was acquired successfully
 */
bool acquire_pin_or_set_null(const GpioPinSpec& pin_spec, GpioDescriptor** gpio_descriptor);

/**
 * Safely acquire the native pin value.
 * Set to GPIO_NUM_NC if the descriptor is null.
 * @param[in] descriptor Pin descriptor to acquire
 * @return Native pin number
 */
gpio_num_t get_native_pin(GpioDescriptor* descriptor);

/**
 * Returns true if the given pin is inverted.
 * @param[in] descriptor Pin descriptor to check, nullable
 */
bool is_pin_inverted(GpioDescriptor* descriptor);
