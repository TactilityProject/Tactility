#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include <memory>

// Display (ST7789P3 on this board)
constexpr auto DISPLAY_SPI_HOST = SPI2_HOST;
constexpr auto DISPLAY_PIN_CS = GPIO_NUM_15;
constexpr auto DISPLAY_PIN_DC = GPIO_NUM_2;
constexpr auto DISPLAY_HORIZONTAL_RESOLUTION = 240;
constexpr auto DISPLAY_VERTICAL_RESOLUTION = 320;
constexpr auto DISPLAY_DRAW_BUFFER_HEIGHT = (DISPLAY_VERTICAL_RESOLUTION / 10);
constexpr auto DISPLAY_DRAW_BUFFER_SIZE = (DISPLAY_HORIZONTAL_RESOLUTION * DISPLAY_DRAW_BUFFER_HEIGHT);
constexpr auto DISPLAY_BACKLIGHT_PIN = GPIO_NUM_27;

// Touch (XPT2046, resistive, shared SPI with display)
constexpr auto TOUCH_MISO_PIN = GPIO_NUM_12;
constexpr auto TOUCH_MOSI_PIN = GPIO_NUM_13;
constexpr auto TOUCH_SCK_PIN = GPIO_NUM_14;
constexpr auto TOUCH_CS_PIN = GPIO_NUM_33;
constexpr auto TOUCH_IRQ_PIN = GPIO_NUM_36;


std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();