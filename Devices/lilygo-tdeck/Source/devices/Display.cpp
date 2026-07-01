#include "Display.h"

#include <Gt911Touch.h>
#include <PwmBacklight.h>
#include <St7789Display.h>
#include <tactility/check.h>
#include <tactility/device.h>

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c0");
    check(i2c);
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        i2c,
        240,
        320,
        true,
        true,
        false,
        GPIO_NUM_NC,
        GPIO_NUM_16
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    St7789Display::Configuration panel_configuration = {
        .horizontalResolution = 320,
        .verticalResolution = 240,
        .gapX = 0,
        .gapY = 0,
        .swapXY = true,
        .mirrorX = true,
        .mirrorY = false,
        .invertColor = true,
        .bufferSize = LCD_BUFFER_SIZE,
        .touch = createTouch(),
        .backlightDutyFunction = driver::pwmbacklight::setBacklightDuty,
        .resetPin = GPIO_NUM_NC,
        .lvglSwapBytes = false,
        .buffSpiram = false // Enabling leads to crashes when refreshing App Hub list
    };

    auto spi_configuration = std::make_shared<St7789Display::SpiConfiguration>(St7789Display::SpiConfiguration {
        .spiHostDevice = LCD_SPI_HOST,
        .csPin = LCD_PIN_CS,
        .dcPin = LCD_PIN_DC,
        .pixelClockFrequency = 62'500'000,
        .transactionQueueDepth = 10
    });

    return std::make_shared<St7789Display>(panel_configuration, spi_configuration);
}
