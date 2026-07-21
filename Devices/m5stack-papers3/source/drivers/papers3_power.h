#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/error.h>

struct Papers3PowerConfig {
    struct GpioPinSpec pin_charge_status;
    struct GpioPinSpec pin_usb_detect;
    struct GpioPinSpec pin_power_off;
    /** Tone generator used for the power-off confirmation beep */
    struct Device* pwm;
};

/** Charge IC status: true while the battery is charging. */
error_t papers3_power_is_charging(struct Device* device, bool* charging);

/**
 * @brief Whether USB VBUS is currently present.
 * @warning Ported as-is from the old deprecated-HAL driver, which never actually wired this into
 * its metrics and flagged it "TODO: Fix USB Detection" - the read itself hasn't been independently
 * re-verified against real hardware here either, just carried forward with the same caveat.
 */
error_t papers3_power_is_usb_connected(struct Device* device, bool* connected);

/** Beeps twice, then pulses the shutdown pin (does not return on success). */
error_t papers3_power_off(struct Device* device);

#ifdef __cplusplus
}
#endif
