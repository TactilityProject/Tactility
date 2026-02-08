#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/display/DisplayDriver.h>

#include <esp_lcd_types.h>
#include <lvgl.h>

#include <driver/spi_common.h>

#define UNPHONE_LCD_SPI_HOST SPI2_HOST
#define UNPHONE_LCD_PIN_CS GPIO_NUM_48
#define UNPHONE_LCD_PIN_DC GPIO_NUM_47
#define UNPHONE_LCD_PIN_RESET GPIO_NUM_46
#define UNPHONE_LCD_SPI_FREQUENCY 27000000
#define UNPHONE_LCD_HORIZONTAL_RESOLUTION 320
#define UNPHONE_LCD_VERTICAL_RESOLUTION 480
#define UNPHONE_LCD_DRAW_BUFFER_HEIGHT (UNPHONE_LCD_VERTICAL_RESOLUTION / 15)

class Hx8357Display : public tt::hal::display::DisplayDevice {

    uint8_t* buffer = nullptr;
    lv_display_t* lvglDisplay = nullptr;
    std::shared_ptr<tt::hal::touch::TouchDevice> touchDevice;
    std::shared_ptr<tt::hal::display::DisplayDriver> nativeDisplay;

    class Hx8357Driver : public tt::hal::display::DisplayDriver {
    public:
        tt::hal::display::ColorFormat getColorFormat() const override { return tt::hal::display::ColorFormat::RGB888; }
        uint16_t getPixelWidth() const override { return UNPHONE_LCD_HORIZONTAL_RESOLUTION; }
        uint16_t getPixelHeight() const override { return UNPHONE_LCD_VERTICAL_RESOLUTION; }
        bool drawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void* pixelData) override;
    };

public:

    std::string getName() const final { return "HX8357"; }
    std::string getDescription() const final { return "SPI display"; }

    bool start() override;

    bool stop() override;

    bool supportsLvgl() const override { return true; }

    bool startLvgl() override;

    bool stopLvgl() override;

    std::shared_ptr<tt::hal::touch::TouchDevice> getTouchDevice() override;

    lv_display_t* getLvglDisplay() const override { return lvglDisplay; }

    // TODO: Set to true after fixing UnPhoneDisplayDriver
    bool supportsDisplayDriver() const override { return false; }

    std::shared_ptr<tt::hal::display::DisplayDriver> getDisplayDriver() override {
        if (nativeDisplay == nullptr) {
            nativeDisplay = std::make_shared<Hx8357Driver>();
        }
        assert(nativeDisplay != nullptr);
        return nativeDisplay;
    }
};

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
