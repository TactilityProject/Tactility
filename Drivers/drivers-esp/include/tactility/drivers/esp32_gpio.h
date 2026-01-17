#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>

struct esp32_gpio_config {
    uint8_t gpio_count;
};

#define esp32_gpio_api gpio_controller_api

int esp32_gpio_init(const struct device* device);

int esp32_gpio_deinit(const struct device* device);

#ifdef __cplusplus
}
#endif
