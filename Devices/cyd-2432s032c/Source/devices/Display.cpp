#include "Display.h"

#include <Gt911Touch.h>
#include <Ili934xDisplay.h>
#include <PwmBacklight.h>
#include <tactility/check.h>
#include <tactility/device.h>

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c0");
    check(i2c);
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        i2c,
        LCD_HORIZONTAL_RESOLUTION,
        LCD_VERTICAL_RESOLUTION
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    Ili934xDisplay::Configuration panel_configuration = {
        .horizontalResolution = LCD_HORIZONTAL_RESOLUTION,
        .verticalResolution = LCD_VERTICAL_RESOLUTION,
        .gapX = 0,
        .gapY = 0,
        .swapXY = true,
        .mirrorX = true,
        .mirrorY = true,
        .invertColor = true,
        .swapBytes = true,
        .bufferSize = LCD_BUFFER_SIZE,
        .touch = createTouch(),
        .backlightDutyFunction = driver::pwmbacklight::setBacklightDuty,
        .resetPin = GPIO_NUM_NC,
        .rgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB
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
