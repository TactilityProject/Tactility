#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <tactility/freertos/freertos.h>
#include "gpio.h"

struct i2c_controller_api {
    bool (*read)(struct device* dev, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);
    bool (*write)(struct device* dev, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);
    bool (*write_read)(struct device* dev, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout);
};

bool i2c_controller_read(struct device* dev, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);

bool i2c_controller_write(struct device* dev, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

bool i2c_controller_write_read(struct device* dev, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout);

#ifdef __cplusplus
}
#endif
