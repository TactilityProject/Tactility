#pragma once
#include <driver/gpio.h>
#include <Tactility/hal/display/DisplayDevice.h>

#include "driver/spi_common.h"

class St7789i8080Display;

constexpr auto DISPLAY_CS = GPIO_NUM_6;
constexpr auto DISPLAY_DC = GPIO_NUM_7;
constexpr auto DISPLAY_WR = GPIO_NUM_8;
constexpr auto DISPLAY_RD = GPIO_NUM_NC;
constexpr auto DISPLAY_RST = GPIO_NUM_NC;
constexpr auto DISPLAY_BL = GPIO_NUM_38;
constexpr auto DISPLAY_I80_D0 = GPIO_NUM_48;
constexpr auto DISPLAY_I80_D1 = GPIO_NUM_47;
constexpr auto DISPLAY_I80_D2 = GPIO_NUM_39;
constexpr auto DISPLAY_I80_D3 = GPIO_NUM_40;
constexpr auto DISPLAY_I80_D4 = GPIO_NUM_41;
constexpr auto DISPLAY_I80_D5 = GPIO_NUM_42;
constexpr auto DISPLAY_I80_D6 = GPIO_NUM_45;
constexpr auto DISPLAY_I80_D7 = GPIO_NUM_46;
constexpr auto DISPLAY_HORIZONTAL_RESOLUTION = 240;
constexpr auto DISPLAY_VERTICAL_RESOLUTION = 320;

// Touch (XPT2046, resistive, shared SPI with display)
constexpr auto TOUCH_MISO_PIN = GPIO_NUM_4;
constexpr auto TOUCH_MOSI_PIN = GPIO_NUM_3;
constexpr auto TOUCH_SCK_PIN = GPIO_NUM_1;
constexpr auto TOUCH_CS_PIN = GPIO_NUM_2;
constexpr auto TOUCH_IRQ_PIN = GPIO_NUM_9;

// Factory function for registration
std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
