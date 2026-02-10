// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>
#include <stdbool.h>
#include <stdint.h>

#define GPIO_FLAGS_MASK 0x1f

#define GPIO_PIN_NONE -1

#define GPIO_PIN_SPEC_NONE ((struct GpioPinSpec) { NULL, 0, GPIO_FLAG_NONE })

#define GPIO_FLAG_NONE 0
#define GPIO_FLAG_ACTIVE_HIGH (0 << 0)
#define GPIO_FLAG_ACTIVE_LOW (1 << 0)
#define GPIO_FLAG_DIRECTION_INPUT (1 << 1)
#define GPIO_FLAG_DIRECTION_OUTPUT (1 << 2)
#define GPIO_FLAG_DIRECTION_INPUT_OUTPUT (GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_DIRECTION_OUTPUT)
#define GPIO_FLAG_PULL_UP (0 << 3)
#define GPIO_FLAG_PULL_DOWN (1 << 4)
#define GPIO_FLAG_INTERRUPT_BITMASK (0b111 << 5) // 3 bits to hold the values [0, 5]
#define GPIO_FLAG_INTERRUPT_FROM_OPTIONS(options) (gpio_int_type_t)((options & GPIO_FLAG_INTERRUPT_BITMASK) >> 5)
#define GPIO_FLAG_INTERRUPT_TO_OPTIONS(options, interrupt) (options | (interrupt << 5))

typedef enum {
    GPIO_INTERRUPT_DISABLE = 0,
    GPIO_INTERRUPT_POS_EDGE = 1,
    GPIO_INTERRUPT_NEG_EDGE = 2,
    GPIO_INTERRUPT_ANY_EDGE = 3,
    GPIO_INTERRUPT_LOW_LEVEL = 4,
    GPIO_INTERRUPT_HIGH_LEVEL = 5,
    GPIO_MAX,
} GpioInterruptType;

enum GpioOwnerType {
    /** @brief Pin is unclaimed/free */
    GPIO_OWNER_NONE,
    /** @brief Pin is owned by a hog */
    GPIO_OWNER_HOG,
    /** @brief Pin is claimed by a regular consumer */
    GPIO_OWNER_GPIO,
    /** @brief Pin is owned by SPI. This is a special case because of CS pin transfer from hog to SPI controller. */
    GPIO_OWNER_SPI
};

/** The index of a GPIO pin on a GPIO Controller */
typedef uint8_t gpio_pin_t;

/** Specifies the configuration flags for a GPIO pin (or set of pins) */
typedef uint16_t gpio_flags_t;

/**
 * Specifies a pin and its properties for a specific GPIO controller.
 * Used by the devicetree, drivers and application code to refer to GPIO pins and acquire them via the gpio_controller API.
 */
struct GpioPinSpec {
    /** GPIO device controlling the pin */
    struct Device* gpio_controller;
    /** The pin's number on the device */
    gpio_pin_t pin;
    /** The pin's configuration flags as specified in devicetree */
    gpio_flags_t flags;
};

#ifdef __cplusplus
}
#endif
