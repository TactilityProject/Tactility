#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <memory>

constexpr auto LCD_SPI_HOST = SPI2_HOST;
constexpr auto LCD_PIN_CS = GPIO_NUM_5;
constexpr auto LCD_PIN_DC = GPIO_NUM_15;
constexpr auto LCD_HORIZONTAL_RESOLUTION = 320;
constexpr auto LCD_VERTICAL_RESOLUTION = 240;
constexpr auto LCD_BUFFER_HEIGHT = LCD_VERTICAL_RESOLUTION / 10;
constexpr auto LCD_BUFFER_SIZE = LCD_HORIZONTAL_RESOLUTION * LCD_BUFFER_HEIGHT;

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
