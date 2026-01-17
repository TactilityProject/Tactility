#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "gpio.h"

struct gpio_controller_config {
    uint8_t gpio_count;
};

struct gpio_controller_api {
    bool (*set_level)(const struct device*, gpio_pin_t pin, bool high);
    bool (*get_level)(const struct device*, gpio_pin_t pin, bool* high);
    bool (*set_options)(const struct device*, gpio_pin_t pin, gpio_flags_t options);
    bool (*get_options)(const struct device*, gpio_pin_t pin, gpio_flags_t* options);
};

#define GPIO_API(dev) ((struct gpio_controller_api*)dev->api)
#define GPIO_CONFIG(dev) ((struct gpio_controller_config*)dev->config)

bool gpio_controller_set_level(const struct device* dev, gpio_pin_t pin, bool high);
bool gpio_controller_get_level(const struct device* dev, gpio_pin_t pin, bool* high);
bool gpio_controller_set_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t options);
bool gpio_controller_get_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t* options);

inline bool gpio_set_options_config(const struct gpio_pin_config* config) {
    return gpio_controller_set_options(config->port, config->pin, config->dt_flags);
}

#ifdef __cplusplus
}
#endif
