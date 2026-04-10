#include "Display.h"
#include "Xpt2046Touch.h"
#include <Ili934xDisplay.h>
#include <PwmBacklight.h>

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch(esp_lcd_spi_bus_handle_t spiDevice) {
    auto configuration = std::make_unique<Xpt2046Touch::Configuration>(
        spiDevice,
        TOUCH_CS_PIN,
        LCD_HORIZONTAL_RESOLUTION,
        LCD_VERTICAL_RESOLUTION,
        true,  // swapXY
        false,   // mirrorX
        true    // mirrorY
    );
    return std::make_shared<Xpt2046Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto spi_configuration = std::make_shared<Ili934xDisplay::SpiConfiguration>(Ili934xDisplay::SpiConfiguration {
        .spiHostDevice = LCD_SPI_HOST,
        .csPin = LCD_PIN_CS,
        .dcPin = LCD_PIN_DC,
        .pixelClockFrequency = 40'000'000,
        .transactionQueueDepth = 10
    });

    Ili934xDisplay::Configuration panel_configuration = {
        .horizontalResolution = LCD_HORIZONTAL_RESOLUTION,
        .verticalResolution = LCD_VERTICAL_RESOLUTION,
        .gapX = 0,
        .gapY = 0,
        .swapXY = true,
        .mirrorX = true,
        .mirrorY = true,
        .invertColor = false,
        .swapBytes = true,
        .bufferSize = LCD_BUFFER_SIZE,
        .touch = createTouch(spi_configuration->spiHostDevice),
        .backlightDutyFunction = driver::pwmbacklight::setBacklightDuty,
        .resetPin = LCD_PIN_RST,
        .rgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB
    };

    return std::make_shared<Ili934xDisplay>(panel_configuration, spi_configuration, true);
}
