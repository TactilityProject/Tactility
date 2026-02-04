// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>

#define GPIO_DRIVER_API(driver) ((struct GpioControllerApi*)driver->api)

extern "C" {

error_t gpio_controller_set_level(Device* device, gpio_pin_t pin, bool high) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->set_level(device, pin, high);
}

error_t gpio_controller_get_level(Device* device, gpio_pin_t pin, bool* high) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->get_level(device, pin, high);
}

error_t gpio_controller_set_options(Device* device, gpio_pin_t pin, gpio_flags_t options) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->set_options(device, pin, options);
}

error_t gpio_controller_get_options(Device* device, gpio_pin_t pin, gpio_flags_t* options) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->get_options(device, pin, options);
}

error_t gpio_controller_get_pin_count(struct Device* device, uint32_t* count) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->get_pin_count(device, count);
}

extern const struct DeviceType GPIO_CONTROLLER_TYPE { 0 };

}
