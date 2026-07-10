// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Properties that a power supply device can report.
 */
enum PowerSupplyProperty {
    /** [0, 1]: whether the battery is currently charging */
    POWER_SUPPLY_PROP_IS_CHARGING,
    /** mA: battery current, positive while charging, negative while discharging */
    POWER_SUPPLY_PROP_CURRENT,
    /** mV: battery voltage */
    POWER_SUPPLY_PROP_VOLTAGE,
    /** [0, 100]: battery charge level */
    POWER_SUPPLY_PROP_CAPACITY,
};

/**
 * @brief Holds the value of a single power supply property.
 */
union PowerSupplyPropertyValue {
    int int_value;
};

/**
 * @brief API for power supply drivers.
 *
 * @note supports_charge_control(), supports_quick_charge() and supports_power_off() gate
 * their respective functions: when a capability is not supported, the driver's implementation
 * of the related getter/setter is expected to be a NO-OP (getters returning false/0).
 */
struct PowerSupplyApi {
    /**
     * @brief Checks whether a property is supported by this device.
     * @param[in] device the power supply device
     * @param[in] property the property to check
     * @return true if the property is supported
     */
    bool (*supports_property)(struct Device* device, enum PowerSupplyProperty property);

    /**
     * @brief Gets the current value of a property.
     * @param[in] device the power supply device
     * @param[in] property the property to read
     * @param[out] out_value the property value
     * @retval ERROR_NOT_SUPPORTED when the property is not supported
     * @retval ERROR_NOT_FOUND when the property is (temporarily) not available
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_property)(struct Device* device, enum PowerSupplyProperty property, union PowerSupplyPropertyValue* out_value);

    /**
     * @brief Checks whether this device supports enabling/disabling charging.
     */
    bool (*supports_charge_control)(struct Device* device);

    /**
     * @brief Checks whether charging is currently allowed.
     */
    bool (*is_allowed_to_charge)(struct Device* device);

    /**
     * @brief Enables or disables charging.
     * @retval ERROR_NOT_SUPPORTED when charge control is not supported
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_allowed_to_charge)(struct Device* device, bool allowed);

    /**
     * @brief Checks whether this device supports quick charging.
     */
    bool (*supports_quick_charge)(struct Device* device);

    /**
     * @brief Checks whether quick charging is currently enabled.
     */
    bool (*is_quick_charge_enabled)(struct Device* device);

    /**
     * @brief Enables or disables quick charging.
     * @retval ERROR_NOT_SUPPORTED when quick charging is not supported
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_quick_charge_enabled)(struct Device* device, bool enabled);

    /**
     * @brief Checks whether this device can power off the system.
     */
    bool (*supports_power_off)(struct Device* device);

    /**
     * @brief Powers off the system.
     * @retval ERROR_NOT_SUPPORTED when power off is not supported
     * @retval ERROR_NONE when the operation was successful (this call typically does not return)
     */
    error_t (*power_off)(struct Device* device);
};

/**
 * @brief Checks whether a property is supported using the specified power supply device.
 */
bool power_supply_supports_property(struct Device* device, enum PowerSupplyProperty property);

/**
 * @brief Gets the current value of a property using the specified power supply device.
 */
error_t power_supply_get_property(struct Device* device, enum PowerSupplyProperty property, union PowerSupplyPropertyValue* out_value);

/**
 * @brief Checks whether the specified power supply device supports enabling/disabling charging.
 */
bool power_supply_supports_charge_control(struct Device* device);

/**
 * @brief Checks whether charging is currently allowed on the specified power supply device.
 */
bool power_supply_is_allowed_to_charge(struct Device* device);

/**
 * @brief Enables or disables charging on the specified power supply device.
 */
error_t power_supply_set_allowed_to_charge(struct Device* device, bool allowed);

/**
 * @brief Checks whether the specified power supply device supports quick charging.
 */
bool power_supply_supports_quick_charge(struct Device* device);

/**
 * @brief Checks whether quick charging is currently enabled on the specified power supply device.
 */
bool power_supply_is_quick_charge_enabled(struct Device* device);

/**
 * @brief Enables or disables quick charging on the specified power supply device.
 */
error_t power_supply_set_quick_charge_enabled(struct Device* device, bool enabled);

/**
 * @brief Checks whether the specified power supply device can power off the system.
 */
bool power_supply_supports_power_off(struct Device* device);

/**
 * @brief Powers off the system using the specified power supply device.
 */
error_t power_supply_power_off(struct Device* device);

extern const struct DeviceType POWER_SUPPLY_TYPE;

#ifdef __cplusplus
}
#endif
