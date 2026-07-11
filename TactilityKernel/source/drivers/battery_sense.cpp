// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/adc_controller.h>
#include <tactility/drivers/battery_sense.h>
#include <tactility/drivers/power_supply.h>

#include <new>

// Raw-to-millivolt conversion assumes a 12-bit ADC, since the generic ADC API doesn't expose resolution.
#define ADC_MAX_RAW 4095

// The power-supply child's config pointer isn't set; it reads its settings from its parent's config instead.
#define GET_PARENT_CONFIG(device) ((const BatterySenseConfig*)device_get_parent(device)->config)

extern "C" {

static bool supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_VOLTAGE;
}

static error_t get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_VOLTAGE) {
        return ERROR_NOT_SUPPORTED;
    }

    const auto* config = GET_PARENT_CONFIG(device);
    int raw;
    error_t error = adc_channel_read_raw(&config->io_channel, &raw, portMAX_DELAY);
    if (error != ERROR_NONE) {
        return error;
    }

    int64_t adc_mv = ((int64_t)raw * config->reference_voltage_mv) / ADC_MAX_RAW;
    out_value->int_value = (int)((adc_mv * config->multiplier) / 1000);
    return ERROR_NONE;
}

static bool supports_charge_control(Device*) { return false; }
static bool is_allowed_to_charge(Device*) { return false; }
static error_t set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_quick_charge(Device*) { return false; }
static bool is_quick_charge_enabled(Device*) { return false; }
static error_t set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_power_off(Device*) { return false; }
static error_t power_off(Device*) { return ERROR_NOT_SUPPORTED; }

static constexpr PowerSupplyApi BATTERY_SENSE_POWER_SUPPLY_API = {
    .supports_property = supports_property,
    .get_property = get_property,
    .supports_charge_control = supports_charge_control,
    .is_allowed_to_charge = is_allowed_to_charge,
    .set_allowed_to_charge = set_allowed_to_charge,
    .supports_quick_charge = supports_quick_charge,
    .is_quick_charge_enabled = is_quick_charge_enabled,
    .set_quick_charge_enabled = set_quick_charge_enabled,
    .supports_power_off = supports_power_off,
    .power_off = power_off,
};

// Registered (driver_construct_add() in kernel_init.cpp) so driver_bind() has a valid ->internal,
// but never matched against a devicetree node: battery_sense_driver wires it up directly by pointer.
Driver battery_sense_power_supply_driver = {
    .name = "battery-sense-power-supply",
    .compatible = (const char*[]) { "battery-sense-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &BATTERY_SENSE_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = nullptr,
    .internal = nullptr
};

struct BatterySenseInternal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "battery-sense-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &battery_sense_power_supply_driver);

    error = device_add(child);
    if (error != ERROR_NONE) {
        device_destruct(child);
        delete child;
        return error;
    }

    error = device_start(child);
    if (error != ERROR_NONE) {
        device_remove(child);
        device_destruct(child);
        delete child;
        return error;
    }

    out_child = child;
    return ERROR_NONE;
}

static void destroy_power_supply_child(Device* child) {
    check(device_stop(child) == ERROR_NONE);
    check(device_remove(child) == ERROR_NONE);
    check(device_destruct(child) == ERROR_NONE);
    delete child;
}

static error_t start(Device* device) {
    auto* internal = new(std::nothrow) BatterySenseInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        delete internal;
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = (BatterySenseInternal*)device_get_driver_data(device);
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver battery_sense_driver = {
    .name = "battery-sense",
    .compatible = (const char*[]) { "battery-sense", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = nullptr,
    .internal = nullptr
};

}
