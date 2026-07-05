#pragma once

#include <Tactility/hal/touch/TouchDevice.h>
#include <tactility/device.h>

// Capacitive touch for the LilyGO T-Deck Max's Hynitron CST66xx controller at
// I2C 0x1A. (The vendor docs label it "CST328", but the vendor HynTouch driver
// probes cst66xx first and that is what answers here.) This is a minimal,
// single-touch port of the vendor's hyn_cst66xx.c protocol for LVGL pointer input.
class Cst66xxTouch final : public tt::hal::touch::TouchDevice {

public:

    struct Configuration {
        ::Device* i2cController;
        uint8_t address;
        uint16_t width;
        uint16_t height;
        bool swapXy;
        bool mirrorX;
        bool mirrorY;
    };

    explicit Cst66xxTouch(const Configuration& configuration) : configuration(configuration) {}

    std::string getName() const override { return "CST66xx"; }
    std::string getDescription() const override { return "CST66xx I2C capacitive touch"; }

    bool start() override;
    bool stop() override;

    bool supportsLvgl() const override { return true; }
    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;
    lv_indev_t* getLvglIndev() override { return indev; }

    bool supportsTouchDriver() override { return false; }
    std::shared_ptr<tt::hal::touch::TouchDriver> getTouchDriver() override { return nullptr; }

private:

    Configuration configuration;
    lv_indev_t* indev = nullptr;
    int16_t lastX = 0;
    int16_t lastY = 0;
    // The CST66xx also reports the three bezel touch-keys (heart / speech-bubble /
    // paper-airplane). bezelKeyDown latches a press so we act once on the rising
    // edge rather than every poll while the key is held.
    bool bezelKeyDown = false;

    bool readPoint(int16_t& x, int16_t& y);
    void handleBezelKey(uint8_t keyId, bool pressed);
    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);
};
