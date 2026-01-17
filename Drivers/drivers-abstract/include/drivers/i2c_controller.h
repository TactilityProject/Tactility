#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "gpio.h"

struct i2c_controller_config {
    uint32_t clock_frequency;
    struct gpio_pin_config pin_sda;
    struct gpio_pin_config pin_scl;
};

struct i2c_controller_api {
    bool (*master_read)(struct device* dev, uint8_t address, uint8_t* data, size_t dataSize); // TODO: add timeout
    bool (*master_write)(struct device* dev, uint8_t address, const uint8_t* data, uint16_t dataSize); // TODO: add timeout
};

#ifdef __cplusplus
}
#endif
