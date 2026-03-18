#pragma once

#include "Tactility/hal/display/DisplayDevice.h"
#include <memory>
#include <driver/gpio.h>
#include <driver/spi_common.h>

constexpr auto LCD_SPI_HOST = SPI2_HOST;
constexpr auto LCD_PIN_CS = GPIO_NUM_41;
constexpr auto LCD_PIN_DC = GPIO_NUM_45;
constexpr auto LCD_PIN_RESET = GPIO_NUM_21;
constexpr auto LCD_HORIZONTAL_RESOLUTION = 135;
constexpr auto LCD_VERTICAL_RESOLUTION = 240;
constexpr auto LCD_BUFFER_HEIGHT = LCD_VERTICAL_RESOLUTION / 3;
constexpr auto LCD_BUFFER_SIZE = LCD_HORIZONTAL_RESOLUTION * LCD_BUFFER_HEIGHT;
constexpr auto LCD_SPI_TRANSFER_SIZE_LIMIT = LCD_BUFFER_SIZE * LV_COLOR_DEPTH / 8;

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
