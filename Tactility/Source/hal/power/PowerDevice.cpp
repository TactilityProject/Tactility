// SPDX-License-Identifier: Apache-2.0
#include <Tactility/hal/power/PowerDevice.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/power_supply.h>

#include <format>

namespace tt::hal::power {

#define GET_POWER_DEVICE(device) (static_cast<PowerDevice*>(device_get_driver_data(device)))

static PowerDevice::MetricType toMetricType(PowerSupplyProperty property) {
    switch (property) {
        case POWER_SUPPLY_PROP_IS_CHARGING:
            return PowerDevice::MetricType::IsCharging;
        case POWER_SUPPLY_PROP_CURRENT:
            return PowerDevice::MetricType::Current;
        case POWER_SUPPLY_PROP_VOLTAGE:
            return PowerDevice::MetricType::BatteryVoltage;
        case POWER_SUPPLY_PROP_CAPACITY:
        default:
            return PowerDevice::MetricType::ChargeLevel;
    }
}

static int toIntValue(PowerDevice::MetricType type, const PowerDevice::MetricData& data) {
    switch (type) {
        case PowerDevice::MetricType::IsCharging:
            return data.valueAsBool ? 1 : 0;
        case PowerDevice::MetricType::Current:
            return data.valueAsInt32;
        case PowerDevice::MetricType::BatteryVoltage:
            return static_cast<int>(data.valueAsUint32);
        case PowerDevice::MetricType::ChargeLevel:
        default:
            return data.valueAsUint8;
    }
}

static bool apiSupportsProperty(::Device* device, PowerSupplyProperty property) {
    return GET_POWER_DEVICE(device)->supportsMetric(toMetricType(property));
}

static error_t apiGetProperty(::Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* outValue) {
    auto* power_device = GET_POWER_DEVICE(device);
    auto metric_type = toMetricType(property);
    if (!power_device->supportsMetric(metric_type)) {
        return ERROR_NOT_SUPPORTED;
    }
    PowerDevice::MetricData data;
    if (!power_device->getMetric(metric_type, data)) {
        return ERROR_NOT_FOUND;
    }
    outValue->int_value = toIntValue(metric_type, data);
    return ERROR_NONE;
}

static bool apiSupportsChargeControl(::Device* device) {
    return GET_POWER_DEVICE(device)->supportsChargeControl();
}

static bool apiIsAllowedToCharge(::Device* device) {
    return GET_POWER_DEVICE(device)->isAllowedToCharge();
}

static error_t apiSetAllowedToCharge(::Device* device, bool allowed) {
    auto* power_device = GET_POWER_DEVICE(device);
    if (!power_device->supportsChargeControl()) {
        return ERROR_NOT_SUPPORTED;
    }
    power_device->setAllowedToCharge(allowed);
    return ERROR_NONE;
}

static bool apiSupportsQuickCharge(::Device* device) {
    return GET_POWER_DEVICE(device)->supportsQuickCharge();
}

static bool apiIsQuickChargeEnabled(::Device* device) {
    return GET_POWER_DEVICE(device)->isQuickChargeEnabled();
}

static error_t apiSetQuickChargeEnabled(::Device* device, bool enabled) {
    auto* power_device = GET_POWER_DEVICE(device);
    if (!power_device->supportsQuickCharge()) {
        return ERROR_NOT_SUPPORTED;
    }
    power_device->setQuickChargeEnabled(enabled);
    return ERROR_NONE;
}

static bool apiSupportsPowerOff(::Device* device) {
    return GET_POWER_DEVICE(device)->supportsPowerOff();
}

static error_t apiPowerOff(::Device* device) {
    auto* power_device = GET_POWER_DEVICE(device);
    if (!power_device->supportsPowerOff()) {
        return ERROR_NOT_SUPPORTED;
    }
    power_device->powerOff();
    return ERROR_NONE;
}

static error_t startDevice(::Device*) { return ERROR_NONE; }
static error_t stopDevice(::Device*) { return ERROR_NONE; }

static PowerSupplyApi powerSupplyApi {
    .supports_property = apiSupportsProperty,
    .get_property = apiGetProperty,
    .supports_charge_control = apiSupportsChargeControl,
    .is_allowed_to_charge = apiIsAllowedToCharge,
    .set_allowed_to_charge = apiSetAllowedToCharge,
    .supports_quick_charge = apiSupportsQuickCharge,
    .is_quick_charge_enabled = apiIsQuickChargeEnabled,
    .set_quick_charge_enabled = apiSetQuickChargeEnabled,
    .supports_power_off = apiSupportsPowerOff,
    .power_off = apiPowerOff,
};

static const char* powerSupplyCompatible[] = { "hal-power-device", nullptr };

static Driver powerSupplyDriver {
    .name = "hal-power-device",
    .compatible = powerSupplyCompatible,
    .start_device = startDevice,
    .stop_device = stopDevice,
    .api = &powerSupplyApi,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

/** Registers the "hal-power-device" driver with the kernel on first use. */
static Driver& getPowerSupplyDriver() {
    static const bool registered = [] {
        check(driver_construct_add(&powerSupplyDriver) == ERROR_NONE);
        return true;
    }();
    (void)registered;
    return powerSupplyDriver;
}

void PowerDevice::createPowerSupplyDevice() {
    kernelDeviceName = std::format("power-supply-{}", getId());
    kernelDevice.name = kernelDeviceName.c_str();
    check(device_construct(&kernelDevice) == ERROR_NONE);
    check(device_add(&kernelDevice) == ERROR_NONE);
    device_set_driver(&kernelDevice, &getPowerSupplyDriver());
    check(device_start(&kernelDevice) == ERROR_NONE);
    device_set_driver_data(&kernelDevice, this);
}

void PowerDevice::destroyPowerSupplyDevice() {
    device_set_driver_data(&kernelDevice, nullptr);
    check(device_stop(&kernelDevice) == ERROR_NONE);
    check(device_remove(&kernelDevice) == ERROR_NONE);
    check(device_destruct(&kernelDevice) == ERROR_NONE);
}

PowerDevice::PowerDevice() {
    createPowerSupplyDevice();
}

PowerDevice::~PowerDevice() {
    destroyPowerSupplyDevice();
}

}
