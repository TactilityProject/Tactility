#include <drivers/gpio_controller.h>

bool gpio_controller_set_level(const struct device* dev, gpio_pin_t pin, bool high) {
    return GPIO_API(dev)->set_level(dev, pin, high);
}

bool gpio_controller_get_level(const struct device* dev, gpio_pin_t pin, bool* high) {
    return GPIO_API(dev)->get_level(dev, pin, high);
}

bool gpio_controller_set_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t options) {
    return GPIO_API(dev)->set_options(dev, pin, options);
}

bool gpio_controller_get_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t* options) {
    return GPIO_API(dev)->get_options(dev, pin, options);
}
