// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration for a whole-device power-off request line.
 *
 * The driver leaves the line high-impedance while idle and drives it to its logical active
 * level when a power-off is requested. The polarity comes from the pin spec: with
 * GPIO_FLAG_ACTIVE_LOW the driver pulls the line physically low, while an active-high pin is
 * driven physically high. This allows both open-drain active-low lines (e.g. ESP-Mosaico's
 * POWER_SWITCH signal to its SAM8108 power controller) and active-high power controllers.
 *
 * The pin spec may also carry pull flags. Any direction flags in it are replaced by the
 * driver, which needs an input (high-impedance) idle state and an output asserted state.
 */
struct GpioPowerOffConfig {
    /** Shutdown request pin; its flags select the request polarity */
    struct GpioPinSpec pin;
    /**
     * How long to keep the line asserted, in milliseconds, before giving up when the power
     * did not actually drop. The line is released again in that case so the device is not
     * left in a half-off state.
     */
    uint32_t assert_timeout_ms;
};

#ifdef __cplusplus
}
#endif
