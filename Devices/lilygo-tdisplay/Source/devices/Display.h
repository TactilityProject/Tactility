#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <memory>
#include <driver/gpio.h>
#include <driver/spi_common.h>

constexpr auto LCD_SPI_HOST = SPI2_HOST;
constexpr auto LCD_PIN_CS = GPIO_NUM_5;
constexpr auto LCD_PIN_DC = GPIO_NUM_16;
constexpr auto LCD_PIN_RESET = GPIO_NUM_23;
constexpr auto LCD_PIN_BACKLIGHT = GPIO_NUM_4;
constexpr auto LCD_HORIZONTAL_RESOLUTION = 135;
constexpr auto LCD_VERTICAL_RESOLUTION = 240;
constexpr auto LCD_BUFFER_HEIGHT = LCD_VERTICAL_RESOLUTION / 3;
constexpr auto LCD_BUFFER_SIZE = LCD_HORIZONTAL_RESOLUTION * LCD_BUFFER_HEIGHT;

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
