// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Axp2101Config {
    /** Address on bus */
    uint8_t address;
    /** LDOx output voltage in mV, applied at driver start when non-zero, independently of the
     *  matching xEnabled flag. 0 leaves the channel's voltage untouched. Field order here MUST
     *  match the property order in bindings/x-powers,axp2101.yaml: the devicetree compiler
     *  emits positional (non-designated) initializers, so a mismatch silently shifts every
     *  value into the wrong field. */
    uint16_t aldo1_millivolt;
    bool aldo1_enabled;
    uint16_t aldo2_millivolt;
    bool aldo2_enabled;
    uint16_t aldo3_millivolt;
    bool aldo3_enabled;
    uint16_t aldo4_millivolt;
    bool aldo4_enabled;
    uint16_t bldo1_millivolt;
    bool bldo1_enabled;
    uint16_t bldo2_millivolt;
    bool bldo2_enabled;
    uint16_t cpusldo_millivolt;
    bool cpusldo_enabled;
    uint16_t dldo1_millivolt;
    bool dldo1_enabled;
    uint16_t dldo2_millivolt;
    bool dldo2_enabled;
};

/** Switchable/adjustable DCDC (buck) converters of the AXP2101. */
enum Axp2101Dcdc {
    AXP2101_DCDC1,
    AXP2101_DCDC2,
    AXP2101_DCDC3,
    AXP2101_DCDC4,
    AXP2101_DCDC5,
};

/** Switchable/adjustable LDO regulators of the AXP2101. */
enum Axp2101Ldo {
    AXP2101_ALDO1,
    AXP2101_ALDO2,
    AXP2101_ALDO3,
    AXP2101_ALDO4,
    AXP2101_BLDO1,
    AXP2101_BLDO2,
    AXP2101_CPUSLDO,
    AXP2101_DLDO1,
    AXP2101_DLDO2,
};

/** Checks whether a DCDC converter is currently enabled. */
error_t axp2101_is_dcdc_enabled(struct Device* device, enum Axp2101Dcdc dcdc, bool* enabled);

/** Enables or disables a DCDC converter. */
error_t axp2101_set_dcdc_enabled(struct Device* device, enum Axp2101Dcdc dcdc, bool enabled);

/**
 * @brief Sets the output voltage of a DCDC converter.
 * @retval ERROR_INVALID_ARGUMENT when millivolts is outside the converter's supported range/step
 * (DCDC1: 1500-3400mV/100mV; DCDC2: 500-1200mV/10mV or 1220-1540mV/20mV;
 * DCDC3: 500-1200mV/10mV, 1220-1540mV/20mV or 1600-3400mV/100mV;
 * DCDC4: 500-1200mV/10mV or 1220-1840mV/20mV; DCDC5: 1200mV, or 1400-3700mV/100mV)
 */
error_t axp2101_set_dcdc_voltage(struct Device* device, enum Axp2101Dcdc dcdc, uint16_t millivolts);

/** Checks whether an LDO regulator is currently enabled. */
error_t axp2101_is_ldo_enabled(struct Device* device, enum Axp2101Ldo ldo, bool* enabled);

/** Enables or disables an LDO regulator. */
error_t axp2101_set_ldo_enabled(struct Device* device, enum Axp2101Ldo ldo, bool enabled);

/**
 * @brief Sets the output voltage of an LDO regulator.
 * @retval ERROR_INVALID_ARGUMENT when millivolts is outside the regulator's supported range/step
 * (ALDO1-4/BLDO1-2: 500-3500mV/100mV; CPUSLDO: 500-1400mV/50mV; DLDO1-2: 500-3400mV/100mV)
 */
error_t axp2101_set_ldo_voltage(struct Device* device, enum Axp2101Ldo ldo, uint16_t millivolts);

/** Battery voltage in millivolts (0 when no battery is connected). */
error_t axp2101_get_battery_voltage(struct Device* device, uint16_t* millivolts);

/** Whether a battery is currently detected. */
error_t axp2101_is_battery_connected(struct Device* device, bool* connected);

/** Whether VBUS (USB power) is currently present and usable. */
error_t axp2101_is_vbus_present(struct Device* device, bool* present);

/** VBUS voltage in millivolts (0 when VBUS is not present). */
error_t axp2101_get_vbus_voltage(struct Device* device, uint16_t* millivolts);

/** Whether the battery is currently charging. */
error_t axp2101_is_charging(struct Device* device, bool* charging);

/** Whether the charger is allowed to charge the battery. */
error_t axp2101_is_charge_enabled(struct Device* device, bool* enabled);

/** Enables or disables battery charging. */
error_t axp2101_set_charge_enabled(struct Device* device, bool enabled);

/** Powers off the system (does not return on success). */
error_t axp2101_power_off(struct Device* device);

#ifdef __cplusplus
}
#endif
