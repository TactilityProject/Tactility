#pragma once

#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/TactilityCore.h>
#include <Tactility/Thread.h>
#include <driver/i2c.h>

#include "ft6x36/FT6X36.h"

class Ft6x36Touch final : public tt::hal::touch::TouchDevice {

public:

    class Configuration {
    public:

        Configuration(
            i2c_port_t port,
            gpio_num_t pinInterrupt,
            uint16_t width,
            uint16_t height
        ) : port(port),
            pinInterrupt(pinInterrupt),
            width(width),
            height(height)
        {}

        i2c_port_t port;
        gpio_num_t pinInterrupt;
        uint16_t width;
        uint16_t height;
};

private:

    std::unique_ptr<Configuration> configuration;
    lv_indev_t* deviceHandle = nullptr;
    FT6X36 driver = FT6X36(configuration->port, configuration->pinInterrupt);
    std::shared_ptr<tt::Thread> driverThread;
    bool interruptDriverThread = false;
    tt::Mutex mutex;
    std::shared_ptr<tt::hal::touch::TouchDriver> nativeTouch;

    lv_point_t lastPoint = { .x = 0, .y = 0 };
    lv_indev_state_t lastState = LV_INDEV_STATE_RELEASED;

    bool shouldInterruptDriverThread() const;

    void driverThreadMain();

    static void touchReadCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:

    explicit Ft6x36Touch(std::unique_ptr<Configuration> inConfiguration);
    ~Ft6x36Touch() override;

    std::string getName() const override { return "FT6x36"; }
    std::string getDescription() const override { return "FT6x36 I2C touch driver"; }

    bool start() override;
    bool stop() override;

    bool supportsLvgl() const override { return true; }
    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    lv_indev_t* getLvglIndev() override { return deviceHandle; }

    class Ft6TouchDriver : public tt::hal::touch::TouchDriver {
    public:
        const Ft6x36Touch& parent;
        Ft6TouchDriver(const Ft6x36Touch& parent) : parent(parent) {}

        bool getTouchedPoints(uint16_t* x, uint16_t* y, uint16_t* _Nullable strength, uint8_t* pointCount, uint8_t maxPointCount) {
            auto lock = parent.mutex.asScopedLock();
            lock.lock();
            if (parent.lastState == LV_INDEV_STATE_PRESSED) {
                *x = parent.lastPoint.x;
                *y = parent.lastPoint.y;
                *pointCount = 1;
                return true;
            } else {
                *pointCount = 0;
                return false;
            }
        }
    };

    bool supportsTouchDriver() override { return true; }

    std::shared_ptr<tt::hal::touch::TouchDriver> _Nullable getTouchDriver() override { return nativeTouch; }
};
