#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <device.h>
#include <drivers/i2c_controller.h>

// Inherit base config
#define esp32_i2c_config i2c_controller_config

// Inherit base API
#define esp32_i2c_api i2c_controller_api

int esp32_i2c_init(const struct device* device);

int esp32_i2c_deinit(const struct device* device);

#ifdef __cplusplus
}
#endif
