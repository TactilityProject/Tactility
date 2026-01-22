#include <Tactility/drivers/GpioController.h>
#include <Tactility/Driver.h>

#define GPIO_DRIVER_API(driver) ((struct GpioControllerApi*)driver->api)

extern "C" {

bool gpio_controller_set_level(Device* device, gpio_pin_t pin, bool high) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->set_level(device, pin, high);
}

bool gpio_controller_get_level(Device* device, gpio_pin_t pin, bool* high) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->get_level(device, pin, high);
}

bool gpio_controller_set_options(Device* device, gpio_pin_t pin, gpio_flags_t options) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->set_options(device, pin, options);
}

bool gpio_controller_get_options(Device* device, gpio_pin_t pin, gpio_flags_t* options) {
    const auto* driver = device_get_driver(device);
    return GPIO_DRIVER_API(driver)->get_options(device, pin, options);
}

}
