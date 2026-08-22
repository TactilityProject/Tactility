#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/error.h>

struct XteinkX4PowerConfig {
    /** USB VBUS detect pin: 1 = USB connected (charging), 0 = battery only */
    struct GpioPinSpec pin_usb_detect;
    /** Battery MOSFET latch pin: driven HIGH to keep the battery rail on, LOW to power off */
    struct GpioPinSpec pin_power_off;
    /** Power button pin (active-low, pulled up) */
    struct GpioPinSpec pin_power_button;
    /** Hold duration (ms) of the power button before the device enters sleep */
    uint32_t power_button_hold_ms;
    /** Minimum boot hold (ms); 0 disables battery cold-boot wake verification */
    uint32_t wake_hold_ms;
};

/**
 * @brief Whether USB VBUS is currently present.
 * @note This is the reference firmware's charge indicator as well: the X4's charge
 * IC exposes no status pin, so "charging" is inferred from VBUS presence (see the
 * sunwoods schematic thread: U0RXD reads ~1.0V idle and ~3.3V with USB plugged in).
 */
error_t xteink_x4_power_is_usb_connected(struct Device* device, bool* connected);

/**
 * @brief Whether the power button is currently pressed.
 */
error_t xteink_x4_power_is_power_button_pressed(struct Device* device, bool* pressed);

/**
 * @brief Drives the battery latch LOW and holds it, powering the board off on battery.
 * @warning This only cuts the battery rail. While USB VBUS is present the board stays
 * powered from USB (the LDO is disabled but the MCU keeps running), so callers should
 * gate this on the USB-detect state when a full shutdown is required.
 * @note Does not return on a successful battery power-off.
 */
error_t xteink_x4_power_off(struct Device* device);

/**
 * @brief Enters deep sleep. Drops the battery latch first (powering the MCU off on
 * battery, so the power button physically re-engages the latch to boot again) and arms
 * the power button as the deep-sleep GPIO wake source for the USB-powered case.
 * @warning Callers are responsible for parking the display before calling this.
 * @note Does not return on a successful deep sleep.
 */
error_t xteink_x4_power_enter_sleep(struct Device* device);

#ifdef __cplusplus
}
#endif
