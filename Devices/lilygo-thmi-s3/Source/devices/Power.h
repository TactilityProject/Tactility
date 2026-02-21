#pragma once

#include <memory>
#include <Tactility/hal/power/PowerDevice.h>
#include <driver/gpio.h>

constexpr auto TDISPLAY_S3_POWEREN_GPIO = GPIO_NUM_10;
constexpr auto TDISPLAY_S3_POWERON_GPIO = GPIO_NUM_14;

std::shared_ptr<tt::hal::power::PowerDevice> createPower();
