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
    int (*read)(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);
    int (*write)(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);
    int (*write_read)(struct Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout);
};

int i2c_controller_read(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);

int i2c_controller_write(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

int i2c_controller_write_read(struct Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout);

extern const struct DeviceType I2C_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
