// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/drivers/gpio_descriptor.h>

#include <cstdlib>
#include <new>

#define GPIO_INTERNAL_API(driver) ((struct GpioControllerApi*)(driver)->api)

extern "C" {

struct GpioControllerData {
    Mutex mutex {};
    uint32_t pin_count;
    GpioDescriptor* descriptors = nullptr;
    void* controller_context;

    explicit GpioControllerData(
        uint32_t pin_count, void* controller_context
    ) : pin_count(pin_count), controller_context(controller_context) {
        mutex_construct(&mutex);
    }

    error_t init_descriptors(Device* device) {
        descriptors = static_cast<GpioDescriptor*>(calloc(pin_count, sizeof(GpioDescriptor)));
        if (!descriptors) return ERROR_OUT_OF_MEMORY;
        for (uint32_t i = 0; i < pin_count; ++i) {
            descriptors[i].controller = device;
            descriptors[i].pin = static_cast<gpio_pin_t>(i);
            descriptors[i].owner_type = GPIO_OWNER_NONE;
            descriptors[i].controller_context = this->controller_context;
        }
        return ERROR_NONE;
    }

    ~GpioControllerData() {
        if (descriptors != nullptr) {
            free(descriptors);
        }
        mutex_destruct(&mutex);
    }
};

GpioDescriptor* gpio_descriptor_acquire(
    Device* controller,
    gpio_pin_t pin_number,
    GpioOwnerType owner
) {
    check(owner != GPIO_OWNER_NONE);

    auto* data = static_cast<struct GpioControllerData*>(device_get_driver_data(controller));

    mutex_lock(&data->mutex);
    if (pin_number >= data->pin_count) {
        mutex_unlock(&data->mutex);
        return nullptr;
    }

    GpioDescriptor* desc = &data->descriptors[pin_number];
    if (desc->owner_type != GPIO_OWNER_NONE) {
        mutex_unlock(&data->mutex);
        return nullptr;
    }

    desc->owner_type = owner;
    mutex_unlock(&data->mutex);

    return desc;
}

error_t gpio_descriptor_release(GpioDescriptor* descriptor) {
    auto* data = static_cast<struct GpioControllerData*>(device_get_driver_data(descriptor->controller));
    mutex_lock(&data->mutex);
    descriptor->owner_type = GPIO_OWNER_NONE;
    mutex_unlock(&data->mutex);
    return ERROR_NONE;
}

error_t gpio_controller_get_pin_count(Device* device, uint32_t* count) {
    auto* data = static_cast<struct GpioControllerData*>(device_get_driver_data(device));
    mutex_lock(&data->mutex);
    *count = data->pin_count;
    mutex_unlock(&data->mutex);
    return ERROR_NONE;
}

error_t gpio_controller_init_descriptors(Device* device, uint32_t pin_count, void* controller_context) {
    auto* data = new(std::nothrow) GpioControllerData(pin_count, controller_context);
    if (!data) return ERROR_OUT_OF_MEMORY;

    if (data->init_descriptors(device) != ERROR_NONE) {
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;
}

error_t gpio_controller_deinit_descriptors(Device* device) {
    auto* data = static_cast<struct GpioControllerData*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

void* gpio_controller_get_controller_context(Device* device) {
    auto* data = static_cast<struct GpioControllerData*>(device_get_driver_data(device));
    return data->controller_context;
}

error_t gpio_descriptor_set_level(GpioDescriptor* descriptor, bool high) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->set_level(descriptor, high);
}

error_t gpio_descriptor_get_level(GpioDescriptor* descriptor, bool* high) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_level(descriptor, high);
}

error_t gpio_descriptor_set_flags(GpioDescriptor* descriptor, gpio_flags_t flags) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->set_flags(descriptor, flags);
}

error_t gpio_descriptor_get_flags(GpioDescriptor* descriptor, gpio_flags_t* flags) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_flags(descriptor, flags);
}

error_t gpio_descriptor_get_pin_number(GpioDescriptor* descriptor, gpio_pin_t* pin) {
    *pin = descriptor->pin;
    return ERROR_NONE;
}

error_t gpio_descriptor_get_native_pin_number(GpioDescriptor* descriptor, void* pin_number) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_native_pin_number(descriptor, pin_number);
}

error_t gpio_descriptor_add_callback(GpioDescriptor* descriptor, void (*callback)(void*), void* arg) {
    const auto* driver = device_get_driver(descriptor->controller);
    auto* api = GPIO_INTERNAL_API(driver);
    if (!api->add_callback) return ERROR_NOT_SUPPORTED;
    return api->add_callback(descriptor, callback, arg);
}

error_t gpio_descriptor_remove_callback(GpioDescriptor* descriptor) {
    const auto* driver = device_get_driver(descriptor->controller);
    auto* api = GPIO_INTERNAL_API(driver);
    if (!api->remove_callback) return ERROR_NOT_SUPPORTED;
    return api->remove_callback(descriptor);
}

error_t gpio_descriptor_enable_interrupt(GpioDescriptor* descriptor) {
    const auto* driver = device_get_driver(descriptor->controller);
    auto* api = GPIO_INTERNAL_API(driver);
    if (!api->enable_interrupt) return ERROR_NOT_SUPPORTED;
    return api->enable_interrupt(descriptor);
}

error_t gpio_descriptor_disable_interrupt(GpioDescriptor* descriptor) {
    const auto* driver = device_get_driver(descriptor->controller);
    auto* api = GPIO_INTERNAL_API(driver);
    if (!api->disable_interrupt) return ERROR_NOT_SUPPORTED;
    return api->disable_interrupt(descriptor);
}

error_t gpio_descriptor_get_owner_type(GpioDescriptor* descriptor, GpioOwnerType* owner_type) {
    *owner_type = descriptor->owner_type;
    return ERROR_NONE;
}

const DeviceType GPIO_CONTROLLER_TYPE {
    .name = "gpio-controller"
};

}
