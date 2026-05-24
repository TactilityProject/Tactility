#include "Display.h"

#include <Ft6x36Touch.h>
#include <Ili934xDisplay.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto configuration = std::make_unique<Ft6x36Touch::Configuration>(
        I2C_NUM_0,
        LCD_HORIZONTAL_RESOLUTION,
        LCD_VERTICAL_RESOLUTION,
        false,
        false,
        false,
        GPIO_NUM_NC,
        GPIO_NUM_39
    );

    return std::make_shared<Ft6x36Touch>(std::move(configuration));
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
        .backlightDutyFunction = nullptr,
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
