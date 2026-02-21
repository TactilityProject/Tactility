#pragma once

#include <driver/gpio.h>

#include "Tactility/hal/sdcard/SdCardDevice.h"

using tt::hal::sdcard::SdCardDevice;

constexpr auto SD_DIO_CMD = GPIO_NUM_11;
constexpr auto SD_DIO_SCLK = GPIO_NUM_12;
constexpr auto SD_DIO_DATA0 = GPIO_NUM_13;
constexpr auto SD_DIO_NC = GPIO_NUM_NC;

std::shared_ptr<SdCardDevice> createSdCard();