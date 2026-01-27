// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>
#include <stdbool.h>
#include <stdint.h>

#define GPIO_OPTIONS_MASK 0x1f

#define GPIO_ACTIVE_HIGH (0 << 0)
#define GPIO_ACTIVE_LOW (1 << 0)

#define GPIO_DIRECTION_INPUT (1 << 1)
#define GPIO_DIRECTION_OUTPUT (1 << 2)
#define GPIO_DIRECTION_INPUT_OUTPUT (GPIO_DIRECTION_INPUT | GPIO_DIRECTION_OUTPUT)

#define GPIO_PULL_UP (0 << 3)
#define GPIO_PULL_DOWN (1 << 4)

#define GPIO_INTERRUPT_BITMASK (0b111 << 5) // 3 bits to hold the values [0, 5]
#define GPIO_INTERRUPT_FROM_OPTIONS(options) (gpio_int_type_t)((options & GPIO_INTERRUPT_BITMASK) >> 5)
#define GPIO_INTERRUPT_TO_OPTIONS(options, interrupt) (options | (interrupt << 5))

typedef enum {
    GPIO_INTERRUPT_DISABLE = 0,
    GPIO_INTERRUPT_POS_EDGE = 1,
    GPIO_INTERRUPT_NEG_EDGE = 2,
    GPIO_INTERRUPT_ANY_EDGE = 3,
    GPIO_INTERRUPT_LOW_LEVEL = 4,
    GPIO_INTERRUPT_HIGH_LEVEL = 5,
    GPIO__MAX,
} GpioInterruptType;

/** The index of a GPIO pin on a GPIO Controller */
typedef uint8_t gpio_pin_t;

/** Specifies the configuration flags for a GPIO pin (or set of pins) */
typedef uint16_t gpio_flags_t;

/** A configuration for a single GPIO pin */
struct GpioPinConfig {
    /** GPIO device controlling the pin */
    const struct Device* port;
    /** The pin's number on the device */
    gpio_pin_t pin;
    /** The pin's configuration flags as specified in devicetree */
    gpio_flags_t flags;
};

/**
 * Check if the pin is ready to be used.
 *
 * @param pinConfig the specifications of the pin
 * @return true if the pin is ready to be used
 */
static inline bool gpio_is_ready(const struct GpioPinConfig* pinConfig) {
    return device_is_ready(pinConfig->port);
}

#ifdef __cplusplus
}
#endif
