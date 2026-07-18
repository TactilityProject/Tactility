// SPDX-License-Identifier: Apache-2.0
#include "papers3_power.h"

#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/pwm.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <new>

#define TAG "Papers3Power"
#define GET_CONFIG(device) (static_cast<const Papers3PowerConfig*>((device)->config))

// Power-off signal timing, ported from the old deprecated-HAL PaperS3Power::powerOff().
static constexpr int POWER_OFF_PULSE_COUNT = 5;
static constexpr TickType_t POWER_OFF_PULSE_DURATION = pdMS_TO_TICKS(100);
static constexpr int BEEP_FREQUENCY_HZ = 440;
static constexpr TickType_t BEEP_DURATION = pdMS_TO_TICKS(200);
static constexpr TickType_t BEEP_GAP = pdMS_TO_TICKS(100);

extern "C" {

extern Module m5stack_papers3_module;

struct Papers3PowerInternal {
    GpioDescriptor* charge_status_descriptor = nullptr;
    GpioDescriptor* usb_detect_descriptor = nullptr;
    GpioDescriptor* power_off_descriptor = nullptr;
    Device* power_supply_device = nullptr;
};

error_t papers3_power_is_charging(Device* device, bool* charging) {
    auto* internal = static_cast<Papers3PowerInternal*>(device_get_driver_data(device));
    bool level;
    error_t error = gpio_descriptor_get_level(internal->charge_status_descriptor, &level);
    if (error != ERROR_NONE) {
        return error;
    }
    // Charge IC status is active-low: 0 = charging, 1 = full/no USB.
    *charging = !level;
    return ERROR_NONE;
}

error_t papers3_power_is_usb_connected(Device* device, bool* connected) {
    auto* internal = static_cast<Papers3PowerInternal*>(device_get_driver_data(device));
    return gpio_descriptor_get_level(internal->usb_detect_descriptor, connected);
}

static void beep(Device* pwm, int frequency_hz, TickType_t duration) {
    uint32_t period_ns = 1000000000U / static_cast<uint32_t>(frequency_hz);
    if (pwm_set_period(pwm, period_ns) != ERROR_NONE ||
        pwm_set_duty(pwm, period_ns / 2) != ERROR_NONE ||
        pwm_enable(pwm) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start buzzer tone");
        return;
    }
    vTaskDelay(duration);
    pwm_disable(pwm);
}

error_t papers3_power_off(Device* device) {
    LOG_W(TAG, "Power-off requested");
    // Note: callers are responsible for stopping the display (e.g. EPD refresh) before calling
    // this. The beep sequence below (~500ms) provides some lead time, but a full EPD refresh can
    // take up to ~1500ms; the display recovers correctly on next boot via a full-screen clear.

    auto* internal = static_cast<Papers3PowerInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    beep(config->pwm, BEEP_FREQUENCY_HZ, BEEP_DURATION);
    vTaskDelay(BEEP_GAP);
    beep(config->pwm, BEEP_FREQUENCY_HZ, BEEP_DURATION);

    LOG_W(TAG, "Triggering shutdown (sending %d pulses)...", POWER_OFF_PULSE_COUNT);
    for (int i = 0; i < POWER_OFF_PULSE_COUNT; i++) {
        gpio_descriptor_set_level(internal->power_off_descriptor, true);
        vTaskDelay(POWER_OFF_PULSE_DURATION);
        gpio_descriptor_set_level(internal->power_off_descriptor, false);
        vTaskDelay(POWER_OFF_PULSE_DURATION);
    }
    gpio_descriptor_set_level(internal->power_off_descriptor, true); // Final high state

    LOG_W(TAG, "Shutdown signal sent. Waiting for power-off...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    LOG_E(TAG, "Device did not power off as expected");
    return ERROR_NONE;
}

// region Power supply child device

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_IS_CHARGING;
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_IS_CHARGING) {
        return ERROR_NOT_SUPPORTED;
    }
    // device_get_parent() here is the papers3-power device itself (this child's parent).
    bool charging;
    error_t error = papers3_power_is_charging(device_get_parent(device), &charging);
    if (error != ERROR_NONE) {
        return error;
    }
    out_value->int_value = charging ? 1 : 0;
    return ERROR_NONE;
}

static bool ps_supports_charge_control(Device*) { return false; }
static bool ps_is_allowed_to_charge(Device*) { return false; }
static error_t ps_set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_quick_charge(Device*) { return false; }
static bool ps_is_quick_charge_enabled(Device*) { return false; }
static error_t ps_set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_power_off(Device*) { return true; }
static error_t ps_power_off(Device* device) { return papers3_power_off(device_get_parent(device)); }

static constexpr PowerSupplyApi PAPERS3_POWER_SUPPLY_API = {
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

// Registered (driver_construct_add() in module.cpp) so driver_bind() has a valid ->internal, but
// never matched against a devicetree node: papers3_power_driver wires it up directly by pointer.
Driver papers3_power_supply_driver = {
    .name = "papers3-power-supply",
    .compatible = (const char*[]) { "papers3-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &PAPERS3_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &m5stack_papers3_module,
    .internal = nullptr
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "papers3-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &papers3_power_supply_driver);

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

// endregion

// region Driver lifecycle

static error_t acquire_input(const GpioPinSpec& pin, GpioDescriptor** out_descriptor) {
    auto* descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        return ERROR_RESOURCE;
    }
    error_t error = gpio_descriptor_set_flags(descriptor, pin.flags | GPIO_FLAG_DIRECTION_INPUT);
    if (error != ERROR_NONE) {
        gpio_descriptor_release(descriptor);
        return error;
    }
    *out_descriptor = descriptor;
    return ERROR_NONE;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = new(std::nothrow) Papers3PowerInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    if (acquire_input(config->pin_charge_status, &internal->charge_status_descriptor) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure charge-status pin");
        delete internal;
        return ERROR_RESOURCE;
    }

    if (acquire_input(config->pin_usb_detect, &internal->usb_detect_descriptor) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure usb-detect pin");
        gpio_descriptor_release(internal->charge_status_descriptor);
        delete internal;
        return ERROR_RESOURCE;
    }

    internal->power_off_descriptor = gpio_descriptor_acquire(config->pin_power_off.gpio_controller, config->pin_power_off.pin, GPIO_OWNER_GPIO);
    if (internal->power_off_descriptor == nullptr ||
        gpio_descriptor_set_flags(internal->power_off_descriptor, config->pin_power_off.flags | GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure power-off pin");
        if (internal->power_off_descriptor != nullptr) {
            gpio_descriptor_release(internal->power_off_descriptor);
        }
        gpio_descriptor_release(internal->usb_detect_descriptor);
        gpio_descriptor_release(internal->charge_status_descriptor);
        delete internal;
        return ERROR_RESOURCE;
    }
    gpio_descriptor_set_level(internal->power_off_descriptor, false);

    error_t error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        gpio_descriptor_release(internal->power_off_descriptor);
        gpio_descriptor_release(internal->usb_detect_descriptor);
        gpio_descriptor_release(internal->charge_status_descriptor);
        delete internal;
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Papers3PowerInternal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    gpio_descriptor_release(internal->power_off_descriptor);
    gpio_descriptor_release(internal->usb_detect_descriptor);
    gpio_descriptor_release(internal->charge_status_descriptor);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

// endregion

Driver papers3_power_driver = {
    .name = "papers3-power",
    .compatible = (const char*[]) { "m5stack,papers3-power", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &m5stack_papers3_module,
    .internal = nullptr
};

}
