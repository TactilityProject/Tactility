#pragma once

#include <EspLcdTouch.h>
#include <Tactility/TactilityCore.h>
#include <driver/i2c.h>

class St7123Touch final : public EspLcdTouch {

public:

    class Configuration {
    public:

        Configuration(
            i2c_port_t port,
            uint16_t xMax,
            uint16_t yMax,
            bool swapXy = false,
            bool mirrorX = false,
            bool mirrorY = false,
            gpio_num_t pinInterrupt = GPIO_NUM_NC
        ) : port(port),
            xMax(xMax),
            yMax(yMax),
            swapXy(swapXy),
            mirrorX(mirrorX),
            mirrorY(mirrorY),
            pinInterrupt(pinInterrupt)
        {}

        i2c_port_t port;
        uint16_t xMax;
        uint16_t yMax;
        bool swapXy;
        bool mirrorX;
        bool mirrorY;
        gpio_num_t pinInterrupt;
    };

private:

    std::unique_ptr<Configuration> configuration;

    bool createIoHandle(esp_lcd_panel_io_handle_t& outHandle) override;

    bool createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& config, esp_lcd_touch_handle_t& touchHandle) override;

    esp_lcd_touch_config_t createEspLcdTouchConfig() override;

public:

    explicit St7123Touch(std::unique_ptr<Configuration> inConfiguration) : configuration(std::move(inConfiguration)) {
        assert(configuration != nullptr);
    }

    std::string getName() const override { return "ST7123Touch"; }

    std::string getDescription() const override { return "ST7123 I2C touch driver"; }
};
