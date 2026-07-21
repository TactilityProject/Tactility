// SPDX-License-Identifier: Apache-2.0
#include "unphone_power_switch.h"

#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/error_esp32.h>
#include <tactility/log.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#define TAG "UnphonePowerSwitch"
#define GET_CONFIG(device) (static_cast<const UnphonePowerSwitchConfig*>((device)->config))

struct UnphonePowerSwitchInternal {
    GpioDescriptor* descriptor;
    gpio_num_t native_pin;
};

extern "C" {

extern Module unphone_module;

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* descriptor = gpio_descriptor_acquire(config->pin.gpio_controller, config->pin.pin, config->pin.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptor");
        return ERROR_RESOURCE;
    }

    gpio_num_t native_pin;
    if (gpio_descriptor_get_native_pin_number(descriptor, &native_pin) != ERROR_NONE) {
        LOG_E(TAG, "Power switch pin has no native pin number");
        gpio_descriptor_release(descriptor);
        return ERROR_NOT_SUPPORTED;
    }

    // Digital pull resistors are powered down during deep sleep; the RTC domain's own pull
    // resistors are what actually hold the pin state while asleep, so both must be armed
    // here (mirrors the original unPhone power switch init).
    if (rtc_gpio_pullup_en(native_pin) != ESP_OK || rtc_gpio_pulldown_en(native_pin) != ESP_OK) {
        LOG_E(TAG, "Failed to configure RTC pull resistors for power switch");
        gpio_descriptor_release(descriptor);
        return ERROR_RESOURCE;
    }

    auto* internal = new UnphonePowerSwitchInternal { .descriptor = descriptor, .native_pin = native_pin };
    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<UnphonePowerSwitchInternal*>(device_get_driver_data(device));
    gpio_descriptor_release(internal->descriptor);
    delete internal;
    return ERROR_NONE;
}

error_t unphone_power_switch_is_on(Device* device, bool* on) {
    auto* internal = static_cast<UnphonePowerSwitchInternal*>(device_get_driver_data(device));
    return gpio_descriptor_get_level(internal->descriptor, on);
}

error_t unphone_power_switch_enable_wake(Device* device) {
    auto* internal = static_cast<UnphonePowerSwitchInternal*>(device_get_driver_data(device));
    auto esp_error = esp_sleep_enable_ext0_wakeup(internal->native_pin, 1);
    return esp_err_to_error(esp_error);
}

Driver unphone_power_switch_driver = {
    .name = "unphone_power_switch",
    .compatible = (const char*[]) { "unphone,power-switch", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &unphone_module,
    .internal = nullptr
};

}
