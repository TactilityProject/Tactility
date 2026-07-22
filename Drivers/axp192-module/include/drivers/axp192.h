// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Axp192Config {
    /** Address on bus */
    uint8_t address;
    /**
     * Rail voltage/enable settings, applied once at driver start (see axp192_set_rail_voltage()/
     * axp192_set_rail_enabled()). Each "enable" flag is unconditionally applied - false (the yaml
     * default) actively disables the rail, same as every other boolean devicetree property in
     * this codebase (e.g. reset-active-high, swap-xy: absence means false, not "leave alone").
     * A board must explicitly set every rail it needs kept on, including ones a board's hardware
     * happens to power up with by default (e.g. DCDC1, which is the SoC's own supply on some
     * boards) - see m5stack-core2's devicetree for a worked example. Voltage of 0 means "don't
     * call axp192_set_rail_voltage() for this rail" (0mV is outside every rail's valid range).
     */
    uint16_t dcdc1_voltage_mv;
    bool dcdc1_enable;
    uint16_t dcdc2_voltage_mv;
    bool dcdc2_enable;
    uint16_t dcdc3_voltage_mv;
    bool dcdc3_enable;
    uint16_t ldo2_voltage_mv;
    bool ldo2_enable;
    uint16_t ldo3_voltage_mv;
    bool ldo3_enable;
    /** EXTEN has no voltage control (see AXP192_RAIL_EXTEN), hence no exten_voltage_mv field. */
    bool exten_enable;
    /** Configures GPIO1 as the PWM1 output (see axp192_set_gpio1_pwm1_output()). */
    bool gpio1_pwm;
    /** Applied only when gpio1_pwm is true (see axp192_set_pwm1_duty_cycle()). */
    uint8_t gpio1_pwm1_duty_cycle;
};

/** Switchable/adjustable power rails of the AXP192. */
enum Axp192Rail {
    AXP192_RAIL_DCDC1,
    AXP192_RAIL_DCDC2,
    AXP192_RAIL_DCDC3,
    AXP192_RAIL_LDO2,
    AXP192_RAIL_LDO3,
    /** EXTEN output switch; does not support voltage control. */
    AXP192_RAIL_EXTEN,
};

/**
 * @brief Checks whether a power rail is currently enabled.
 */
error_t axp192_is_rail_enabled(struct Device* device, enum Axp192Rail rail, bool* enabled);

/**
 * @brief Enables or disables a power rail.
 */
error_t axp192_set_rail_enabled(struct Device* device, enum Axp192Rail rail, bool enabled);

/**
 * @brief Sets the output voltage of a power rail.
 * @retval ERROR_NOT_SUPPORTED for AXP192_RAIL_EXTEN, which has no voltage control
 * @retval ERROR_INVALID_ARGUMENT when millivolts is outside the rail's supported range
 * (DCDC1/DCDC3: 700-3500mV, DCDC2: 700-2275mV, LDO2/LDO3: 1800-3300mV)
 */
error_t axp192_set_rail_voltage(struct Device* device, enum Axp192Rail rail, uint16_t millivolts);

/** Battery voltage in millivolts. */
error_t axp192_get_battery_voltage(struct Device* device, uint16_t* millivolts);

/** Battery charge current in milliamps (0 when not charging). */
error_t axp192_get_battery_charge_current(struct Device* device, uint16_t* milliamps);

/** Battery discharge current in milliamps (0 when not discharging). */
error_t axp192_get_battery_discharge_current(struct Device* device, uint16_t* milliamps);

/** Battery power in microwatts. */
error_t axp192_get_battery_power(struct Device* device, uint32_t* microwatts);

/** Whether the battery is currently charging. */
error_t axp192_is_charging(struct Device* device, bool* charging);

/** Whether the charger is allowed to charge the battery. */
error_t axp192_is_charge_enabled(struct Device* device, bool* enabled);

/** Enables or disables battery charging. */
error_t axp192_set_charge_enabled(struct Device* device, bool enabled);

/** Powers off the system (does not return on success). */
error_t axp192_power_off(struct Device* device);

/** Configures GPIO1 as the PWM1 output (rather than GPIO/ADC input/output). */
error_t axp192_set_gpio1_pwm1_output(struct Device* device);

/** Sets the PWM1 duty cycle (0 = always low, 255 = always high). */
error_t axp192_set_pwm1_duty_cycle(struct Device* device, uint8_t duty);

#ifdef __cplusplus
}
#endif
