#pragma once

#include "Display.h"

#include <Tactility/hal/display/DisplayDevice.h>
#include <memory>
#include <driver/gpio.h>
#include <driver/spi_common.h>

// Display backlight (PWM)
constexpr auto LCD_PIN_BACKLIGHT = GPIO_NUM_27;

// Display
constexpr auto LCD_SPI_HOST = SPI2_HOST;
constexpr auto LCD_PIN_CS = GPIO_NUM_15;
constexpr auto LCD_PIN_DC = GPIO_NUM_2;
constexpr auto LCD_HORIZONTAL_RESOLUTION = 240;
constexpr auto LCD_VERTICAL_RESOLUTION = 320;
constexpr auto LCD_BUFFER_HEIGHT = LCD_VERTICAL_RESOLUTION / 10;
constexpr auto LCD_BUFFER_SIZE = LCD_HORIZONTAL_RESOLUTION * LCD_BUFFER_HEIGHT;

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
