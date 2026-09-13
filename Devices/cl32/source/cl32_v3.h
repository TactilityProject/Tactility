#pragma once

#include <tactility/freertos/freertos.h>

#include <cstdint>

constexpr uint8_t CL32_V3_FUEL_GAUGE_I2C_ADDRESS = 0x36;
constexpr TickType_t CL32_V3_TIMEOUT = pdMS_TO_TICKS(50);
