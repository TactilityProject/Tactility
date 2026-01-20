#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <Tactility/Device.h>

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

/**
 * @brief Provides a type to hold a GPIO pin index.
 *
 * This reduced-size type is sufficient to record a pin number,
 * e.g. from a devicetree GPIOS property.
 */
typedef uint8_t gpio_pin_t;

/**
 * @brief Identifies a set of pins associated with a port.
 *
 * The pin with index n is present in the set if and only if the bit
 * identified by (1U << n) is set.
 */
typedef uint32_t gpio_pinset_t;

/**
 * @brief Provides a type to hold GPIO devicetree flags.
 *
 * All GPIO flags that can be expressed in devicetree fit in the low 16
 * bits of the full flags field, so use a reduced-size type to record
 * that part of a GPIOS property.
 *
 * The lower 8 bits are used for standard flags. The upper 8 bits are reserved
 * for SoC specific flags.
 */
typedef uint16_t gpio_flags_t;

/**
 * @brief Container for GPIO pin information specified in dts files
 *
 * This type contains a pointer to a GPIO device, pin identifier for a pin
 * controlled by that device, and the subset of pin configuration
 * flags which may be given in devicetree.
 */
struct GpioPinConfig {
    /** GPIO device controlling the pin */
    const struct Device* port;
    /** The pin's number on the device */
    gpio_pin_t pin;
    /** The pin's configuration flags as specified in devicetree */
    gpio_flags_t dt_flags;
};

/**
 * Check if the pin is ready to be used.
 * @param pin_config the specifications of the pin
 * @return true if the pin is ready to be used
 */
static inline bool gpio_is_ready(const struct GpioPinConfig* pin_config) {
    return device_is_ready(pin_config->port);
}

#ifdef __cplusplus
}
#endif
