#pragma once

#include <tactility/drivers/gpio_descriptor.h>

#include <driver/gpio.h>

void release_pin(GpioDescriptor** gpio_descriptor);

bool acquire_pin_or_set_null(const GpioPinSpec& pin_spec, GpioDescriptor** gpio_descriptor);

/**
 * Safely acquire the native pin avalue.
 * Set to GPIO_NUM_NC if the descriptor is null.
 * @param[in] descriptor Pin descriptor to acquire
 * @return Native pin number
 */
gpio_num_t get_native_pin(GpioDescriptor* descriptor);

/**
 * Returns true if the given pin is inverted
 * @param[in] descriptor Pin descriptor to check, nullable
 */
bool is_pin_inverted(GpioDescriptor* descriptor);
