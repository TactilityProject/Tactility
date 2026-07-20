// SPDX-License-Identifier: Apache-2.0
#include "tab5_power_control.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>

#define TAG "Tab5PowerControl"

// GPIO expander 1 (0x44) pins - see the pinout table in Configuration.cpp. Only these three are
// this driver's concern; the rest (WLAN enable, USB-A 5V, nCHG_QC_EN's boot default, charge
// state LED) stay owned by Configuration.cpp's initExpander1().
static constexpr int GPIO_EXP1_PIN_DEVICE_POWER = 4;         // PWROFF_PULSE
static constexpr int GPIO_EXP1_PIN_IP2326_NCHG_QC_EN = 5;    // active-low: LOW = quick charge enabled
static constexpr int GPIO_EXP1_PIN_IP2326_CHG_EN = 7;        // HIGH = charging enabled

struct Tab5PowerControlInternal {
    bool charging_allowed;
    bool quick_charge_enabled;
};

// region PowerSupplyApi

// No battery metrics here - see ina226-module's power-supply device for voltage/capacity.
static bool ps_supports_property(Device*, PowerSupplyProperty) {
    return false;
}

static error_t ps_get_property(Device*, PowerSupplyProperty, PowerSupplyPropertyValue*) {
    return ERROR_NOT_SUPPORTED;
}

static bool ps_supports_charge_control(Device*) {
    return true;
}

static bool ps_is_allowed_to_charge(Device* device) {
    auto* internal = static_cast<Tab5PowerControlInternal*>(device_get_driver_data(device));
    return internal->charging_allowed;
}

static error_t ps_set_allowed_to_charge(Device* device, bool allowed) {
    auto* internal = static_cast<Tab5PowerControlInternal*>(device_get_driver_data(device));
    auto* io_expander1 = device_get_parent(device);

    auto* pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_IP2326_CHG_EN, GPIO_OWNER_GPIO);
    if (pin == nullptr) {
        LOG_W(TAG, "Failed to acquire CHG_EN pin");
        return ERROR_RESOURCE;
    }
    error_t error = gpio_descriptor_set_flags(pin, GPIO_FLAG_DIRECTION_OUTPUT);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_set_level(pin, allowed);
    }
    gpio_descriptor_release(pin);
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Failed to set CHG_EN pin");
        return error;
    }

    internal->charging_allowed = allowed;
    return ERROR_NONE;
}

static bool ps_supports_quick_charge(Device*) {
    return true;
}

static bool ps_is_quick_charge_enabled(Device* device) {
    auto* internal = static_cast<Tab5PowerControlInternal*>(device_get_driver_data(device));
    return internal->quick_charge_enabled;
}

static error_t ps_set_quick_charge_enabled(Device* device, bool enabled) {
    auto* internal = static_cast<Tab5PowerControlInternal*>(device_get_driver_data(device));
    auto* io_expander1 = device_get_parent(device);

    auto* pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_IP2326_NCHG_QC_EN, GPIO_OWNER_GPIO);
    if (pin == nullptr) {
        LOG_W(TAG, "Failed to acquire nCHG_QC_EN pin");
        return ERROR_RESOURCE;
    }
    error_t error = gpio_descriptor_set_flags(pin, GPIO_FLAG_DIRECTION_OUTPUT);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_set_level(pin, !enabled); // active-low
    }
    gpio_descriptor_release(pin);
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Failed to set nCHG_QC_EN pin");
        return error;
    }

    internal->quick_charge_enabled = enabled;
    return ERROR_NONE;
}

static bool ps_supports_power_off(Device*) {
    return true;
}

static error_t ps_power_off(Device* device) {
    auto* io_expander1 = device_get_parent(device);
    auto* pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_DEVICE_POWER, GPIO_OWNER_GPIO);
    if (pin == nullptr) {
        LOG_E(TAG, "Failed to acquire DEVICE_POWER pin");
        return ERROR_RESOURCE;
    }
    for (int i = 0; i < 3; i++) {
        gpio_descriptor_set_level(pin, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_descriptor_set_level(pin, false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    gpio_descriptor_release(pin);
    return ERROR_NONE;
}

static constexpr PowerSupplyApi TAB5_POWER_CONTROL_API = {
    .supports_property = ps_supports_property,
    .get_property = ps_get_property,
    .supports_charge_control = ps_supports_charge_control,
    .is_allowed_to_charge = ps_is_allowed_to_charge,
    .set_allowed_to_charge = ps_set_allowed_to_charge,
    .supports_quick_charge = ps_supports_quick_charge,
    .is_quick_charge_enabled = ps_is_quick_charge_enabled,
    .set_quick_charge_enabled = ps_set_quick_charge_enabled,
    .supports_power_off = ps_supports_power_off,
    .power_off = ps_power_off,
};

// endregion

// region Driver lifecycle

static error_t start(Device* device) {
    auto* internal = static_cast<Tab5PowerControlInternal*>(malloc(sizeof(Tab5PowerControlInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->charging_allowed = false;
    internal->quick_charge_enabled = false;
    device_set_driver_data(device, internal);

    // DEVICE_POWER (PWROFF_PULSE) is only ever pulsed on demand (see ps_power_off) - configure it
    // as an idle-low output up front so it's in a known state (previously done by Configuration.cpp's
    // initExpander1(), now owned here - see that function's comment for why it no longer touches it).
    auto* io_expander1 = device_get_parent(device);
    auto* device_power_pin = gpio_descriptor_acquire(io_expander1, GPIO_EXP1_PIN_DEVICE_POWER, GPIO_OWNER_GPIO);
    if (device_power_pin != nullptr) {
        gpio_descriptor_set_flags(device_power_pin, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(device_power_pin, false);
        gpio_descriptor_release(device_power_pin);
    } else {
        LOG_W(TAG, "Failed to acquire DEVICE_POWER pin at start");
    }

    // Quick charge off by default, matching the deprecated HAL's boot-time default (set via
    // initExpander1(), now owned here for the same reason as above).
    if (ps_set_quick_charge_enabled(device, false) != ERROR_NONE) {
        LOG_W(TAG, "Failed to set default quick-charge state");
    }

    // Enable charging by default, matching the deprecated HAL's Tab5Power constructor.
    if (ps_set_allowed_to_charge(device, true) != ERROR_NONE) {
        LOG_W(TAG, "Failed to enable charging by default");
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Tab5PowerControlInternal*>(device_get_driver_data(device));
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// Defined in module.cpp - this driver is registered directly by m5stack-tab5's own module,
// not a separate Drivers/ module (same pattern as Tab5KeyboardDriver).
extern Module m5stack_tab5_module;

Driver tab5_power_control_driver = {
    .name = "tab5-power-control",
    .compatible = (const char*[]) { "m5stack,tab5-power-control", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &TAB5_POWER_CONTROL_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &m5stack_tab5_module,
    .internal = nullptr
};
