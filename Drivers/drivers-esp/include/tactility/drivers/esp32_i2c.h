#pragma once

#include <hal/i2c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>

struct esp32_i2c_config {
    uint32_t clock_frequency;
    struct gpio_pin_config pin_sda;
    struct gpio_pin_config pin_scl;
    const i2c_port_t port;
};

// Inherit base API
#define esp32_i2c_api i2c_controller_api

int esp32_i2c_init(const struct device* device);

int esp32_i2c_deinit(const struct device* device);

#ifdef __cplusplus
}
#endif
