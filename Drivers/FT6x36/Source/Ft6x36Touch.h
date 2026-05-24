#pragma once

#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/TactilityCore.h>
#include <driver/i2c.h>

#include <EspLcdTouch.h>

class Ft6x36Touch final : public EspLcdTouch {

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
            gpio_num_t pinReset = GPIO_NUM_NC,
            gpio_num_t pinInterrupt = GPIO_NUM_NC,
            unsigned int pinResetLevel = 0,
            unsigned int pinInterruptLevel = 0
        ) : port(port),
            xMax(xMax),
            yMax(yMax),
            swapXy(swapXy),
            mirrorX(mirrorX),
            mirrorY(mirrorY),
            pinReset(pinReset),
            pinInterrupt(pinInterrupt),
            pinResetLevel(pinResetLevel),
            pinInterruptLevel(pinInterruptLevel)
        {}

        i2c_port_t port;
        uint16_t xMax;
        uint16_t yMax;
        bool swapXy;
        bool mirrorX;
        bool mirrorY;
        gpio_num_t pinReset;
        gpio_num_t pinInterrupt;
        unsigned int pinResetLevel;
        unsigned int pinInterruptLevel;
    };

private:

    std::unique_ptr<Configuration> configuration;

    bool createIoHandle(esp_lcd_panel_io_handle_t& outHandle) override;

    bool createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& configuration, esp_lcd_touch_handle_t& panelHandle) override;

    esp_lcd_touch_config_t createEspLcdTouchConfig() override;

public:

    explicit Ft6x36Touch(std::unique_ptr<Configuration> inConfiguration) : configuration(std::move(inConfiguration)) {
        assert(configuration != nullptr);
    }

    std::string getName() const override { return "FT6x36"; }

    std::string getDescription() const override { return "FT6x36 I2C touch driver"; }
};
