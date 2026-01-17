#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <device.h>
#include <drivers/gpio_controller.h>

#define esp32_gpio_config gpio_controller_config
#define esp32_gpio_api gpio_controller_api

int esp32_gpio_init(const struct device* device);

int esp32_gpio_deinit(const struct device* device);

#ifdef __cplusplus
}
#endif
