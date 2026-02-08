#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <driver/spi_common.h>

#include <EspLcdDisplay.h>
#include <functional>

class St7796Display final : public EspLcdDisplay {

public:

    class Configuration {

    public:

        Configuration(
            spi_host_device_t spiHostDevice,
            gpio_num_t csPin,
            gpio_num_t dcPin,
            unsigned int horizontalResolution,
            unsigned int verticalResolution,
            std::shared_ptr<tt::hal::touch::TouchDevice> touch,
            bool swapXY = false,
            bool mirrorX = false,
            bool mirrorY = false,
            bool invertColor = false,
            unsigned int gapX = 0,
            unsigned int gapY = 0,
            uint32_t bufferSize = 0 // Size in pixel count. 0 means default, which is 1/10 of the screen size
        ) : spiHostDevice(spiHostDevice),
            csPin(csPin),
            dcPin(dcPin),
            horizontalResolution(horizontalResolution),
            verticalResolution(verticalResolution),
            swapXY(swapXY),
            mirrorX(mirrorX),
            mirrorY(mirrorY),
            invertColor(invertColor),
            gapX(gapX),
            gapY(gapY),
            bufferSize(bufferSize),
            touch(std::move(touch)) {
            if (this->bufferSize == 0) {
                this->bufferSize = horizontalResolution * verticalResolution / 10;
            }
        }

        spi_host_device_t spiHostDevice;
        gpio_num_t csPin;
        gpio_num_t dcPin;
        gpio_num_t resetPin = GPIO_NUM_NC;
        unsigned int pixelClockFrequency = 80'000'000; // Hertz
        size_t transactionQueueDepth = 2;
        unsigned int horizontalResolution;
        unsigned int verticalResolution;
        bool swapXY = false;
        bool mirrorX = false;
        bool mirrorY = false;
        bool invertColor = false;
        unsigned int gapX = 0;
        unsigned int gapY = 0;
        uint32_t bufferSize = 0; // Size in pixel count. 0 means default, which is 1/10 of the screen size
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        std::function<void(uint8_t)> _Nullable backlightDutyFunction = nullptr;
    };

private:

    std::unique_ptr<Configuration> configuration;

    bool createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) override;

    bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) override;

    lvgl_port_display_cfg_t getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) override;

public:

    explicit St7796Display(std::unique_ptr<Configuration> inConfiguration) :
        EspLcdDisplay(tt::hal::spi::getLock(inConfiguration->spiHostDevice)),
        configuration(std::move(inConfiguration)
    ) {
        assert(configuration != nullptr);
    }

    std::string getName() const override { return "ST7796"; }

    std::string getDescription() const override { return "ST7796 display"; }

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override { return configuration->touch; }

    void setBacklightDuty(uint8_t backlightDuty) override {
        if (configuration->backlightDutyFunction != nullptr) {
            configuration->backlightDutyFunction(backlightDuty);
        }
    }

    void setGammaCurve(uint8_t index) override;

    uint8_t getGammaCurveCount() const override { return 4; };

    bool supportsBacklightDuty() const override { return configuration->backlightDutyFunction != nullptr; }
};

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
