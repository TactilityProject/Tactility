#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Tab5KeyboardConfig {
    /** Fixed 0x6D - not used to probe the bus, kept only for parity with other dynamic devices. */
    uint8_t address;
    /**
     * Wired directly to a native SoC GPIO (not an IO expander) for a real ISR.
     * GPIO_PIN_SPEC_NONE falls back to polling REG_INT_STAT.
     */
    struct GpioPinSpec pin_interrupt;
};

extern struct Driver tab5_keyboard_driver;

/**
 * @brief Constructs (but doesn't start) the keyboard device on i2c2 and registers it with the
 * kernel's hotplug poller. Called once i2c2 is up (see display_detect.cpp).
 */
void tab5_create_keyboard(struct Device* i2c2);

/** @brief Proxy for device_is_started().
 * @deprecated Use device_is_started()
 */
error_t tab5_keyboard_is_attached(struct Device* device);

#ifdef __cplusplus
}
#endif
