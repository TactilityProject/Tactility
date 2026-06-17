#pragma once

#include "./I2cCompat.h"
#include "Tactility/Lock.h"

#include <Tactility/freertoscompat/RTOS.h>

namespace tt::hal::i2c {

constexpr TickType_t defaultTimeout = 10 / portTICK_PERIOD_MS;

/** @return true when a device is detected at the specified address */
bool masterHasDeviceAtAddress(i2c_port_t port, uint8_t address, TickType_t timeout = defaultTimeout);

} // namespace
