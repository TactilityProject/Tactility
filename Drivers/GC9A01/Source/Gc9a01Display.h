#pragma once

#include <EspLcdDisplay.h>
#include <Tactility/hal/display/DisplayDevice.h>

#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_types.h>
#include <functional>
#include <lvgl.h>

class Gc9a01Display final : public EspLcdDisplay {

    std::shared_ptr<tt::Lock> lock;

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
            uint32_t bufferSize = 0, // Size in pixel count. 0 means default, which is 1/10 of the screen size
            lcd_rgb_element_order_t rgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB
        ) : spiHostDevice(spiHostDevice),
            csPin(csPin),
            dcPin(dcPin),
            horizontalResolution(horizontalResolution),
            verticalResolution(verticalResolution),
            swapXY(swapXY),
            mirrorX(mirrorX),
            mirrorY(mirrorY),
            invertColor(invertColor),
            bufferSize(bufferSize),
            rgbElementOrder(rgbElementOrder),
            touch(std::move(touch))
        {
            if (this->bufferSize == 0) {
                this->bufferSize = horizontalResolution * verticalResolution / 10;
            }
        }

        spi_host_device_t spiHostDevice;
        gpio_num_t csPin;
        gpio_num_t dcPin;
        gpio_num_t resetPin = GPIO_NUM_NC;
        unsigned int pixelClockFrequency = 80'000'000; // Hertz
        size_t transactionQueueDepth = 10;
        unsigned int horizontalResolution;
        unsigned int verticalResolution;
        bool swapXY = false;
        bool mirrorX = false;
        bool mirrorY = false;
        bool invertColor = false;
        uint32_t bufferSize = 0; // Size in pixel count. 0 means default, which is 1/10 of the screen size
        lcd_rgb_element_order_t rgbElementOrder;
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        std::function<void(uint8_t)> _Nullable backlightDutyFunction = nullptr;
    };

private:

    std::unique_ptr<Configuration> configuration;

    bool createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) override;

    bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) override;

    lvgl_port_display_cfg_t getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) override;

public:

    explicit Gc9a01Display(std::unique_ptr<Configuration> inConfiguration) :
        EspLcdDisplay(tt::hal::spi::getLock(inConfiguration->spiHostDevice)),
        configuration(std::move(inConfiguration)
    ) {
        assert(configuration != nullptr);
        assert(getLock() != nullptr);
    }

    std::string getName() const override { return "GC9A01"; }

    std::string getDescription() const override { return "GC9A01 display"; }

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override { return configuration->touch; }

    void setBacklightDuty(uint8_t backlightDuty) override {
        if (configuration->backlightDutyFunction != nullptr) {
            configuration->backlightDutyFunction(backlightDuty);
        }
    }

    bool supportsBacklightDuty() const override { return configuration->backlightDutyFunction != nullptr; }
};

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
