// SPDX-License-Identifier: Apache-2.0
#include <drivers/gpio_power_off.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "GpioPowerOff"
#define GET_CONFIG(device) (static_cast<const GpioPowerOffConfig*>((device)->config))

/**
 * Flags for the idle state: the request line must not be driven while idle. The pin-spec
 * flags are kept (including high-impedance, when the controller supports it) so the
 * controller still knows the polarity and pull configuration.
 */
static gpio_flags_t get_idle_flags(const GpioPowerOffConfig* config) {
    return (config->pin.flags & ~GPIO_FLAG_DIRECTION_OUTPUT) | GPIO_FLAG_DIRECTION_INPUT;
}

/**
 * Flags for the asserted state: the line has to be driven, but the polarity still comes from
 * the pin spec, so an active-low line is driven physically low and an active-high line is
 * driven physically high.
 */
static gpio_flags_t get_assert_flags(const GpioPowerOffConfig* config) {
    return (config->pin.flags & ~(GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_HIGH_IMPEDANCE)) | GPIO_FLAG_DIRECTION_OUTPUT;
}

// region PowerSupplyApi

static bool supports_property(Device*, PowerSupplyProperty) { return false; }

static error_t get_property(Device*, PowerSupplyProperty, PowerSupplyPropertyValue*) {
    return ERROR_NOT_SUPPORTED;
}

static bool supports_charge_control(Device*) { return false; }
static bool is_allowed_to_charge(Device*) { return false; }
static error_t set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_quick_charge(Device*) { return false; }
static bool is_quick_charge_enabled(Device*) { return false; }
static error_t set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }

static bool supports_power_off(Device*) { return true; }

static error_t power_off(Device* device) {
    auto* descriptor = static_cast<GpioDescriptor*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    // Assert the line's logical active level. The pin spec supplies the polarity, so this
    // drives an active-low line physically low and an active-high line physically high.
    if (gpio_descriptor_set_level(descriptor, true) != ERROR_NONE ||
        gpio_descriptor_set_flags(descriptor, get_assert_flags(config)) != ERROR_NONE) {
        LOG_E(TAG, "Failed to assert the shutdown request");
        gpio_descriptor_set_flags(descriptor, get_idle_flags(config));
        return ERROR_RESOURCE;
    }

    LOG_I(TAG, "Shutdown requested (holding line for up to %u ms)", (unsigned)config->assert_timeout_ms);
    vTaskDelay(pdMS_TO_TICKS(config->assert_timeout_ms));

    // Still running: the power controller did not act on the request. Release the line so the
    // device is usable again instead of staying asserted forever.
    LOG_E(TAG, "Power did not drop; releasing the shutdown request");
    gpio_descriptor_set_flags(descriptor, get_idle_flags(config));
    return ERROR_RESOURCE;
}

// endregion

static const PowerSupplyApi GPIO_POWER_OFF_API = {
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

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    if (config->assert_timeout_ms == 0) {
        LOG_E(TAG, "assert-timeout-ms must be greater than zero");
        return ERROR_INVALID_ARGUMENT;
    }

    // Idle state is high-impedance: the pin must not drive the shared request line.
    auto* descriptor = gpio_descriptor_acquire(
        config->pin.gpio_controller,
        config->pin.pin,
        get_idle_flags(config),
        GPIO_OWNER_GPIO
    );
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire the shutdown pin");
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, descriptor);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* descriptor = static_cast<GpioDescriptor*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    gpio_descriptor_release(descriptor);
    return ERROR_NONE;
}

extern "C" {

Driver gpio_power_off_driver = {
    .name = "gpio_power_off",
    .compatible = (const char*[]) { "gpio-power-off", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &GPIO_POWER_OFF_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .internal = nullptr
};

} // extern "C"
