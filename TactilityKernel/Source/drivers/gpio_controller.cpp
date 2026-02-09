// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/drivers/gpio_descriptor.h>

#define GPIO_INTERNAL_API(driver) ((struct GpioControllerApi*)driver->api)

extern "C" {

struct GpioControllerData {
    struct Mutex mutex;
    uint32_t pin_count;
    struct GpioDescriptor* descriptors;
};

struct GpioDescriptor* acquire_descriptor(
    struct Device* controller,
    gpio_pin_t pin_number,
    enum GpioOwnerType owner,
    void* owner_context
) {
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
    desc->owner_context = owner_context;
    mutex_unlock(&data->mutex);

    return desc;
}

error_t release_descriptor(struct GpioDescriptor* descriptor) {
    descriptor->owner_type = GPIO_OWNER_NONE;
    descriptor->owner_context = nullptr;
    return ERROR_NONE;
}

error_t gpio_controller_get_pin_count(struct Device* device, uint32_t* count) {
    auto* data = (struct GpioControllerData*)device_get_driver_data(device);
    *count = data->pin_count;
    return ERROR_NONE;
}

error_t gpio_controller_init_descriptors(struct Device* device, uint32_t pin_count, void* controller_context) {
    auto* data = (struct GpioControllerData*)malloc(sizeof(struct GpioControllerData));
    if (!data) return ERROR_OUT_OF_MEMORY;

    data->pin_count = pin_count;
    data->descriptors = (struct GpioDescriptor*)calloc(pin_count, sizeof(struct GpioDescriptor));
    if (!data->descriptors) {
        free(data);
        return ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < pin_count; ++i) {
        data->descriptors[i].controller = device;
        data->descriptors[i].pin = (gpio_pin_t)i;
        data->descriptors[i].owner_type = GPIO_OWNER_NONE;
        data->descriptors[i].controller_context = controller_context;
    }

    mutex_construct(&data->mutex);
    device_set_driver_data(device, data);

    return ERROR_NONE;
}

error_t gpio_controller_deinit_descriptors(struct Device* device) {
    auto* data = (struct GpioControllerData*)device_get_driver_data(device);

    free(data->descriptors);
    mutex_destruct(&data->mutex);
    free(data);
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

error_t gpio_descriptor_set_options(struct GpioDescriptor* descriptor, gpio_flags_t options) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->set_options(descriptor, options);
}

error_t gpio_descriptor_get_options(struct GpioDescriptor* descriptor, gpio_flags_t* options) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_options(descriptor, options);
}

error_t gpio_descriptor_get_pin_number(struct GpioDescriptor* descriptor, gpio_pin_t* pin) {
    const auto* driver = device_get_driver(descriptor->controller);
    return GPIO_INTERNAL_API(driver)->get_pin_number(descriptor, pin);
}

const struct DeviceType GPIO_CONTROLLER_TYPE {
    .name = "gpio-controller"
};

}
