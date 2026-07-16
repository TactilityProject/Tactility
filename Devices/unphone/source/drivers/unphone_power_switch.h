#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/error.h>

struct UnphonePowerSwitchConfig {
    struct GpioPinSpec pin;
};

/** @brief Reads whether the physical power switch is currently in the "on" position. */
error_t unphone_power_switch_is_on(struct Device* device, bool* on);

/**
 * @brief Arms deep-sleep wakeup so the device wakes when the switch moves to "on".
 * Only applies to the deep sleep entered right after this call.
 */
error_t unphone_power_switch_enable_wake(struct Device* device);

#ifdef __cplusplus
}
#endif
