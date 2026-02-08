#pragma once

#include <Tactility/hal/display/DisplayDevice.h>

#include <driver/gpio.h>
#include <driver/spi_common.h>

constexpr auto LCD_SPI_HOST = SPI2_HOST;
constexpr auto LCD_PIN_CS = GPIO_NUM_40;
constexpr auto LCD_PIN_DC = GPIO_NUM_41; // RS
constexpr auto LCD_HORIZONTAL_RESOLUTION = 240;
constexpr auto LCD_VERTICAL_RESOLUTION = 320;
constexpr auto LCD_BUFFER_HEIGHT = LCD_VERTICAL_RESOLUTION / 10;
constexpr auto LCD_BUFFER_SIZE = LCD_HORIZONTAL_RESOLUTION * LCD_BUFFER_HEIGHT;

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
