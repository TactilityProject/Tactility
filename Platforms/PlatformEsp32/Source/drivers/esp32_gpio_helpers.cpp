#include <tactility/drivers/esp32_gpio_helpers.h>
#include <tactility/drivers/gpio_controller.h>

#define TAG "gpio"

void release_pin(GpioDescriptor** gpio_descriptor) {
    if (*gpio_descriptor == nullptr) return;
    check(gpio_descriptor_release(*gpio_descriptor) == ERROR_NONE);
    *gpio_descriptor = nullptr;
}

bool acquire_pin_or_set_null(const GpioPinSpec& pin_spec, GpioDescriptor** gpio_descriptor) {
    if (pin_spec.gpio_controller == nullptr) {
        *gpio_descriptor = nullptr;
        return true;
    }
    *gpio_descriptor = gpio_descriptor_acquire(pin_spec.gpio_controller, pin_spec.pin, GPIO_OWNER_GPIO);
    if (*gpio_descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire pin %u from %s", pin_spec.pin, pin_spec.gpio_controller->name);
    }

    return *gpio_descriptor != nullptr;
}

gpio_num_t get_native_pin(GpioDescriptor* descriptor) {
    if (descriptor != nullptr) {
        gpio_num_t pin;
        check(gpio_descriptor_get_native_pin_number(descriptor, &pin) == ERROR_NONE);
        return pin;
    } else {
        return GPIO_NUM_NC;
    }
}

bool is_pin_inverted(GpioDescriptor* descriptor) {
    if (!descriptor) return false;
    gpio_flags_t flags;
    check(gpio_descriptor_get_flags(descriptor, &flags) == ERROR_NONE);
    return (flags & GPIO_FLAG_ACTIVE_LOW) != 0;
}
