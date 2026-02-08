#pragma once

#include <EspLcdDisplay.h>
#include <Tactility/hal/display/DisplayDevice.h>

#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_types.h>

class Ssd1306Display final : public EspLcdDisplay {

public:

    class Configuration {

    public:

        Configuration(
            i2c_port_t port,
            uint8_t deviceAddress,
            gpio_num_t resetPin,
            unsigned int horizontalResolution,  // Typically 128
            unsigned int verticalResolution,    // 32 or 64
            std::shared_ptr<tt::hal::touch::TouchDevice> touch = nullptr,
            bool invertColor = false
        ) : port(port),
            deviceAddress(deviceAddress),
            resetPin(resetPin),
            horizontalResolution(horizontalResolution),
            verticalResolution(verticalResolution),
            invertColor(invertColor),
            touch(std::move(touch))
        {}

        i2c_port_t port;
        uint8_t deviceAddress;
        gpio_num_t resetPin = GPIO_NUM_NC;
        unsigned int horizontalResolution;
        unsigned int verticalResolution;
        bool invertColor = false;
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        uint32_t bufferSize = 0; // Size in pixel count. 0 means default (full screen / 8)
        int gapX = 0; // Column offset
        int gapY = 0; // Not used for SSD1306
    };

private:

    std::unique_ptr<Configuration> configuration;

    bool createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) override;

    bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) override;

    lvgl_port_display_cfg_t getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) override;

public:

    explicit Ssd1306Display(std::unique_ptr<Configuration> inConfiguration) :
        configuration(std::move(inConfiguration))
    {
        assert(configuration != nullptr);
        if (configuration->bufferSize == 0) {
            // For monochrome displays, ESP-LVGL-PORT expects full pixel count
            // It handles the monochrome conversion internally
            configuration->bufferSize = configuration->horizontalResolution * configuration->verticalResolution;
        }
    }

    std::string getName() const override { return "SSD1306"; }

    std::string getDescription() const override { return "SSD1306 monochrome OLED display with ESP-LVGL-PORT monochrome support"; }

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override { return configuration->touch; }

    void setBacklightDuty(uint8_t backlightDuty) override {
        // SSD1306 does not have backlight control
    }

    bool supportsBacklightDuty() const override { return false; }

    void setGammaCurve(uint8_t index) override {
        // SSD1306 does not support gamma curves
    }

    uint8_t getGammaCurveCount() const override { return 0; }
};

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
