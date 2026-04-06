#pragma once

#include "Tactility/hal/touch/TouchDevice.h"
#include "Tactility/hal/touch/TouchDriver.h"
#include "lvgl.h"
#include <driver/gpio.h>
#include <memory>
#include <string>

#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif

#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif

struct Point {
    int x;
    int y;
};

class Xpt2046SoftSpi : public tt::hal::touch::TouchDevice {
public:
    class Configuration {
    public:
        Configuration(
            gpio_num_t mosiPin,
            gpio_num_t misoPin,
            gpio_num_t clkPin,
            gpio_num_t csPin,
            uint16_t xMax = TFT_WIDTH,
            uint16_t yMax = TFT_HEIGHT,
            bool swapXy = false,
            bool mirrorX = false,
            bool mirrorY = false
        ) : mosiPin(mosiPin),
            misoPin(misoPin),
            clkPin(clkPin),
            csPin(csPin),
            xMax(xMax),
            yMax(yMax),
            swapXy(swapXy),
            mirrorX(mirrorX),
            mirrorY(mirrorY)
        {}

        gpio_num_t mosiPin;
        gpio_num_t misoPin;
        gpio_num_t clkPin;
        gpio_num_t csPin;
        uint16_t xMax;
        uint16_t yMax;
        bool swapXy;
        bool mirrorX;
        bool mirrorY;
    };

private:

    class Xpt2046SoftSpiDriver final : public tt::hal::touch::TouchDriver {
        Xpt2046SoftSpi* device;
    public:
        Xpt2046SoftSpiDriver(Xpt2046SoftSpi* device) : device(device) {}
        bool getTouchedPoints(uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* pointCount, uint8_t maxPointCount) override {
            Point point;
            if (device->isTouched() && device->getTouchPoint(point)) {
                *x = point.x;
                *y = point.y;
                *pointCount = 1;
                return true;
            } else {
                *pointCount = 0;
                return false;
            }
        }
    };

    std::unique_ptr<Configuration> configuration;
    lv_indev_t* lvglDevice = nullptr;
    std::shared_ptr<tt::hal::touch::TouchDriver> touchDriver;

    int readSPI(uint8_t command);
    bool readRawPoint(uint16_t& x, uint16_t& y);
    static void touchReadCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:
    explicit Xpt2046SoftSpi(std::unique_ptr<Configuration> inConfiguration);

    // TouchDevice interface
    std::string getName() const final { return "Xpt2046SoftSpi"; }
    std::string getDescription() const final { return "Xpt2046 Soft SPI touch driver"; }

    bool start() override;
    bool stop() override;

    bool supportsLvgl() const override { return true; }
    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    bool supportsTouchDriver() override { return true; }
    bool supportsCalibration() const override { return true; }
    std::shared_ptr<tt::hal::touch::TouchDriver> getTouchDriver() override;
    lv_indev_t* getLvglIndev() override { return lvglDevice; }

    // XPT2046-specific methods
    bool getTouchPoint(Point& point);
    bool isTouched();
};
