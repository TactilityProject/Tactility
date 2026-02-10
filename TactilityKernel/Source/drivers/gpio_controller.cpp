// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/drivers/gpio_descriptor.h>

#include <cstdlib>
#include <new>

#define GPIO_INTERNAL_API(driver) ((struct GpioControllerApi*)driver->api)

extern "C" {

struct GpioControllerData {
    struct Mutex mutex {};
    uint32_t pin_count;
    struct GpioDescriptor* descriptors = nullptr;

    explicit GpioControllerData(uint32_t pin_count) : pin_count(pin_count) {
        mutex_construct(&mutex);
    }

    error_t init_descriptors(Device* device, void* controller_context) {
        descriptors = (struct GpioDescriptor*)calloc(pin_count, sizeof(struct GpioDescriptor));
        if (!descriptors) return ERROR_OUT_OF_MEMORY;
        for (uint32_t i = 0; i < pin_count; ++i) {
            descriptors[i].controller = device;
            descriptors[i].pin = (gpio_pin_t)i;
            descriptors[i].owner_type = GPIO_OWNER_NONE;
            descriptors[i].controller_context = controller_context;
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

struct GpioDescriptor* gpio_descriptor_acquire(
    struct Device* controller,
    gpio_pin_t pin_number,
    enum GpioOwnerType owner
) {
    check(owner != GPIO_OWNER_NONE);

    auto* data = (struct GpioControllerData*)device_get_driver_data(controller);

    mutex_lock(&data->mutex);
    if (pin_number >= data->pin_count) {
        mutex_unlock(&data->mutex);
        return nullptr;
    }

    struct GpioDescriptor* desc = &data->descriptors[pin_number];
    if (desc->owner_type != GPIO_OWNER_NONE) {
        mutex_unlock(&data->mutex);
        return nullptr;
    }

    desc->owner_type = owner;
    mutex_unlock(&data->mutex);

    return desc;
}

error_t gpio_descriptor_release(struct GpioDescriptor* descriptor) {
    descriptor->owner_type = GPIO_OWNER_NONE;
    return ERROR_NONE;
}

error_t gpio_controller_get_pin_count(struct Device* device, uint32_t* count) {
    auto* data = (struct GpioControllerData*)device_get_driver_data(device);
    *count = data->pin_count;
    return ERROR_NONE;
}

error_t gpio_controller_init_descriptors(struct Device* device, uint32_t pin_count, void* controller_context) {
    auto* data = new(std::nothrow) GpioControllerData(pin_count);
    if (!data) return ERROR_OUT_OF_MEMORY;

    if (data->init_descriptors(device, controller_context) != ERROR_NONE) {
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;
}

error_t gpio_controller_deinit_descriptors(struct Device* device) {
    auto* data = static_cast<struct GpioControllerData*>(device_get_driver_data(device));
    delete data;
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

error_t gpio_descriptor_set_level(struct GpioDescriptor* descriptor, bool high) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->set_level(descriptor, high);
}

error_t gpio_descriptor_get_level(struct GpioDescriptor* descriptor, bool* high) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_level(descriptor, high);
}

error_t gpio_descriptor_set_flags(struct GpioDescriptor* descriptor, gpio_flags_t flags) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->set_flags(descriptor, flags);
}

error_t gpio_descriptor_get_flags(struct GpioDescriptor* descriptor, gpio_flags_t* flags) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_flags(descriptor, flags);
}

error_t gpio_descriptor_get_pin_number(struct GpioDescriptor* descriptor, gpio_pin_t* pin) {
    *pin = descriptor->pin;
    return ERROR_NONE;
}

error_t gpio_descriptor_get_native_pin_number(struct GpioDescriptor* descriptor, void* pin_number) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_native_pin_number(descriptor, pin_number);
}

error_t gpio_descriptor_get_owner_type(struct GpioDescriptor* descriptor, GpioOwnerType* owner_type) {
    *owner_type = descriptor->owner_type;
    return ERROR_NONE;
}

const struct DeviceType GPIO_CONTROLLER_TYPE {
    .name = "gpio-controller"
};

}
