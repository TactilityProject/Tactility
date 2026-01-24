// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "Gpio.h"
#include <Tactility/FreeRTOS/FreeRTOS.h>
#include <stdbool.h>
#include <stdint.h>

struct I2cControllerApi {
    bool (*read)(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);
    bool (*write)(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);
    bool (*write_read)(struct Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout);
};

bool i2c_controller_read(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);

bool i2c_controller_write(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

bool i2c_controller_write_read(struct Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout);

extern const struct DeviceType I2C_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
