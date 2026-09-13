#pragma once

#include <tactility/freertos/freertos.h>

#include <cstdint>

constexpr uint8_t CL32_V4_KEYBOARD_PROBE_REGISTER = 0x02;
constexpr uint8_t CL32_V4_CORE_I2C_ADDRESS = 0x08;
constexpr TickType_t CL32_V4_CORE_TIMEOUT = pdMS_TO_TICKS(50);
