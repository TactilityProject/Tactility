// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "gpio.h"

#include <tactility/freertos/freertos.h>
#include <tactility/error.h>

struct I2cControllerApi {
    error_t (*read)(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);
    error_t (*write)(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);
    error_t (*write_read)(struct Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout);
};

error_t i2c_controller_read(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);

error_t i2c_controller_write(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

error_t i2c_controller_write_read(struct Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout);

extern const struct DeviceType I2C_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
