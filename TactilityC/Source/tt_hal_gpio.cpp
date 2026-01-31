#include "tt_hal_gpio.h"
#include <Tactility/hal/gpio/Gpio.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>

extern "C" {

using namespace tt::hal;

static Device* find_first_gpio_controller() {
    Device* device_result = nullptr;
    for_each_device_of_type(&GPIO_CONTROLLER_TYPE, &device_result, [](Device* device, void* context) {
        if (device_is_ready(device)) {
            auto** device_result_ptr = static_cast<Device**>(context);
            *device_result_ptr = device;
            return false;
        }
        return true;
    });
    return device_result;
}

bool tt_hal_gpio_get_level(GpioPin pin) {
    Device* device_result = find_first_gpio_controller();
    if (device_result == nullptr) return false;
    bool pin_state = false;
    if (gpio_controller_get_level(device_result, pin, &pin_state) != ERROR_NONE) return false;
    return pin_state;
}

int tt_hal_gpio_get_pin_count() {
    Device* device_result = find_first_gpio_controller();
    if (device_result == nullptr) return 0;
    uint32_t pin_count = 0;
    if (gpio_controller_get_pin_count(device_result, &pin_count) != ERROR_NONE) return 0;
    return (int)pin_count;
}

}
