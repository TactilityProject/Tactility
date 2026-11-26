// SPDX-License-Identifier: Apache-2.0
#include <tactility/Memory.hpp>

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/antenna_switch.h>
#include <tactility/drivers/gpio_antenna_switch.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>
#include <tactility/module.h>

constexpr auto* TAG = "GpioAntennaSwitch";
#define GET_CONFIG(device) (static_cast<const GpioAntennaSwitchConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<GpioAntennaSwitchInternal*>(device_get_driver_data(device)))

extern "C" {

constexpr uint8_t MAX_SELECTOR_GPIO_COUNT = 32;

struct GpioAntennaSwitchInternal {
    Mutex mutex {};
    GpioDescriptor* gpios[MAX_SELECTOR_GPIO_COUNT] {};
    uint8_t gpio_count = 0;

    GpioAntennaSwitchInternal() {
        mutex_construct(&mutex);
    }

    ~GpioAntennaSwitchInternal() {
        mutex_destruct(&mutex);
    }

    void release_gpios() {
        for (uint8_t index = 0; index < gpio_count; index++) {
            if (gpios[index] != nullptr) {
                gpio_descriptor_release(gpios[index]);
                gpios[index] = nullptr;
            }
        }
    }
};

static error_t set_antenna(Device* device, uint32_t antenna) {
    auto* internal = GET_INTERNAL(device);
    if (internal == nullptr) return ERROR_INVALID_STATE;
    if (internal->gpio_count < MAX_SELECTOR_GPIO_COUNT && (antenna >> internal->gpio_count) != 0) {
        return ERROR_OUT_OF_RANGE;
    }

    mutex_lock(&internal->mutex);
    for (uint8_t index = 0; index < internal->gpio_count; index++) {
        const bool high = ((antenna >> index) & 1u) != 0;
        const error_t error = gpio_descriptor_set_level(internal->gpios[index], high);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "%s: failed to set selector GPIO %u", device->name, (unsigned)index);
            mutex_unlock(&internal->mutex);
            return error;
        }
    }
    mutex_unlock(&internal->mutex);
    return ERROR_NONE;
}

static error_t get_antenna(Device* device, uint32_t* antenna) {
    auto* internal = GET_INTERNAL(device);
    if (internal == nullptr) return ERROR_INVALID_STATE;

    uint32_t selected = 0;
    mutex_lock(&internal->mutex);
    for (uint8_t index = 0; index < internal->gpio_count; index++) {
        bool high = false;
        const error_t error = gpio_descriptor_get_level(internal->gpios[index], &high);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "%s: failed to read selector GPIO %u", device->name, (unsigned)index);
            mutex_unlock(&internal->mutex);
            return error;
        }
        if (high) selected |= (uint32_t{1} << index);
    }
    mutex_unlock(&internal->mutex);
    *antenna = selected;
    return ERROR_NONE;
}

static const AntennaSwitchApi GPIO_ANTENNA_SWITCH_API = {
    .set_antenna = set_antenna,
    .get_antenna = get_antenna,
};

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (config->gpios == nullptr || config->gpios_count == 0 || config->gpios_count > MAX_SELECTOR_GPIO_COUNT) {
        LOG_E(TAG, "%s: selector GPIO count must be between 1 and %u", device->name, (unsigned)MAX_SELECTOR_GPIO_COUNT);
        return ERROR_INVALID_ARGUMENT;
    }
    if (config->gpios_count < MAX_SELECTOR_GPIO_COUNT &&
        config->default_antenna >= (uint32_t{1} << config->gpios_count)) {
        LOG_E(TAG, "%s: default antenna index is out of range", device->name);
        return ERROR_OUT_OF_RANGE;
    }

    auto* internal = tt::newObject<GpioAntennaSwitchInternal>();
    if (internal == nullptr) return ERROR_OUT_OF_MEMORY;
    internal->gpio_count = config->gpios_count;

    for (uint8_t index = 0; index < config->gpios_count; index++) {
        const auto& spec = config->gpios[index];
        if (spec.gpio_controller == nullptr || !device_is_ready(spec.gpio_controller)) {
            LOG_E(TAG, "%s: selector GPIO %u controller is not ready", device->name, (unsigned)index);
            internal->release_gpios();
            tt::deleteObject(internal);
            return ERROR_INVALID_STATE;
        }

        auto* descriptor = gpio_descriptor_acquire(
            spec.gpio_controller,
            spec.pin,
            spec.flags | GPIO_FLAG_DIRECTION_OUTPUT,
            GPIO_OWNER_GPIO
        );
        if (descriptor == nullptr) {
            LOG_E(TAG, "%s: failed to acquire selector GPIO %u", device->name, (unsigned)index);
            internal->release_gpios();
            tt::deleteObject(internal);
            return ERROR_RESOURCE;
        }
        internal->gpios[index] = descriptor;
    }

    device_set_driver_data(device, internal);
    const error_t error = set_antenna(device, config->default_antenna);
    if (error != ERROR_NONE) {
        device_set_driver_data(device, nullptr);
        internal->release_gpios();
        tt::deleteObject(internal);
        return error;
    }
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (internal == nullptr) return ERROR_NONE;

    internal->release_gpios();
    device_set_driver_data(device, nullptr);
    tt::deleteObject(internal);
    return ERROR_NONE;
}

Driver gpio_antenna_switch_driver = {
    .name = "gpio_antenna_switch",
    .compatible = (const char*[]) { "tactility,gpio-antenna-switch", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &GPIO_ANTENNA_SWITCH_API,
    .device_type = &ANTENNA_SWITCH_TYPE,
    .internal = nullptr
};

}
