#include "Display.h"

#include <Axp2101.h>
#include <Ft5x06Touch.h>
#include <Ili934xDisplay.h>
#include <Tactility/Logger.h>
#include <Tactility/hal/i2c/I2c.h>

static const auto LOGGER = tt::Logger("CoreS3Display");

static void setBacklightDuty(uint8_t backlightDuty) {
    const uint8_t voltage = 20 + ((8 * backlightDuty) / 255); // [0b00000, 0b11100] - under 20 is too dark
    // TODO: Refactor to use Axp2102 driver subproject. Reference: https://github.com/m5stack/M5Unified/blob/b8cfec7fed046242da7f7b8024a4e92004a51ff7/src/utility/AXP2101_Class.cpp#L42
    if (!tt::hal::i2c::masterWriteRegister(I2C_NUM_0, AXP2101_ADDRESS, 0x99, &voltage, 1, 1000)) { // Sets DLD01
        LOGGER.error("Failed to set display backlight voltage");
    }
}

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto configuration = std::make_unique<Ft5x06Touch::Configuration>(
        I2C_NUM_0,
        LCD_HORIZONTAL_RESOLUTION,
        LCD_VERTICAL_RESOLUTION
    );

    auto touch = std::make_shared<Ft5x06Touch>(std::move(configuration));
    return std::reinterpret_pointer_cast<tt::hal::touch::TouchDevice>(touch);
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    Ili934xDisplay::Configuration panel_configuration = {
        .horizontalResolution = LCD_HORIZONTAL_RESOLUTION,
        .verticalResolution = LCD_VERTICAL_RESOLUTION,
        .gapX = 0,
        .gapY = 0,
        .swapXY = false,
        .mirrorX = false,
        .mirrorY = false,
        .invertColor = true,
        .swapBytes = true,
        .bufferSize = LCD_BUFFER_SIZE,
        .touch = createTouch(),
        .backlightDutyFunction = ::setBacklightDuty,
        .resetPin = GPIO_NUM_NC,
        .rgbElementOrder = LCD_RGB_ELEMENT_ORDER_BGR
    };

    auto spi_configuration = std::make_shared<Ili934xDisplay::SpiConfiguration>(Ili934xDisplay::SpiConfiguration {
        .spiHostDevice = LCD_SPI_HOST,
        .csPin = LCD_PIN_CS,
        .dcPin = LCD_PIN_DC,
        .pixelClockFrequency = 40'000'000,
        .transactionQueueDepth = 10
    });

    return std::make_shared<Ili934xDisplay>(panel_configuration, spi_configuration, true);
}
