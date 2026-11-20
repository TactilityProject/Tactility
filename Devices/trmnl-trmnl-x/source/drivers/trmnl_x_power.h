// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/error.h>

struct TrmnlXPowerConfig {
    /** BQ25616 charger PG output (low while VBUS is good), read through the IO expander */
    struct GpioPinSpec pin_power_good;
    /** BQ25616 charger STAT output (low while charging), read through the IO expander */
    struct GpioPinSpec pin_charge_status;
    /** Enables the charger's OTG boost output (5 V on USB) */
    struct GpioPinSpec pin_otg;
    /** RC network that reveals how many cells are in the pack */
    struct GpioPinSpec pin_battery_detect;
    /** Reset line of the BQ27427 fuel gauge (asserted = low) */
    struct GpioPinSpec pin_gauge_reset;
    /** The fuel gauge */
    struct Device* gauge;
};

/** Number of battery cells found by the detection network */
enum TrmnlXBatteryCount {
    /** No battery: the detection pin stayed high */
    TRMNL_X_BATTERY_NONE = 0,
    TRMNL_X_BATTERY_ONE = 1,
    TRMNL_X_BATTERY_TWO = 2
};

/** Charger status: true while the battery is charging. */
error_t trmnl_x_power_is_charging(struct Device* device, bool* charging);

/** Whether the charger reports a usable VBUS (USB power present). */
error_t trmnl_x_power_is_vbus_good(struct Device* device, bool* vbus_good);

/** Enables or disables the charger's OTG boost output. It is disabled when the driver starts. */
error_t trmnl_x_power_set_otg_enabled(struct Device* device, bool enabled);

/** Reads back whether the OTG boost output is enabled. */
error_t trmnl_x_power_is_otg_enabled(struct Device* device, bool* enabled);

/** The battery cell count that was found when the driver started. */
error_t trmnl_x_power_get_battery_count(struct Device* device, enum TrmnlXBatteryCount* count);

/**
 * @brief Counts the battery cells by timing how long an RC network takes to discharge
 * 
 * @note Every poll is an I2C read of the IO expander, so the measured time includes the bus latency.
 * @param[in] device the trmnl-x-power device
 * @param[out] count the number of cells
 * @retval ERROR_INVALID_STATE when no two consecutive measurements agreed, which means something's borked.
 */
error_t trmnl_x_power_detect_battery_count(struct Device* device, enum TrmnlXBatteryCount* count);

/** Pulses the fuel gauge's reset line (10 ms). The gauge needs time to restart afterwards. */
error_t trmnl_x_power_reset_gauge(struct Device* device);

#ifdef __cplusplus
}
#endif
