#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <memory>

// Display
constexpr auto LCD_SPI_HOST = SPI2_HOST;
constexpr auto LCD_PIN_CS   = GPIO_NUM_15;
constexpr auto LCD_PIN_DC   = GPIO_NUM_2;
constexpr auto LCD_PIN_RST  = GPIO_NUM_NC;  // tied to ESP32 RST
constexpr auto LCD_PIN_CLK  = GPIO_NUM_14;
constexpr auto LCD_PIN_MOSI = GPIO_NUM_13;
constexpr auto LCD_PIN_MISO = GPIO_NUM_12;
constexpr auto LCD_HORIZONTAL_RESOLUTION = 240;
constexpr auto LCD_VERTICAL_RESOLUTION = 320;
constexpr auto LCD_BUFFER_HEIGHT = LCD_VERTICAL_RESOLUTION / 10;
constexpr auto LCD_BUFFER_SIZE = LCD_HORIZONTAL_RESOLUTION * LCD_BUFFER_HEIGHT;

// Backlight
constexpr auto LCD_PIN_BACKLIGHT = GPIO_NUM_27;

// Touch
constexpr auto TOUCH_CS_PIN  = GPIO_NUM_33;
constexpr auto TOUCH_IRQ_PIN = GPIO_NUM_36;

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
