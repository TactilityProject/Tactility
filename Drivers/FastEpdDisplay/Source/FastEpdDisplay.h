#pragma once

#include <Tactility/Lock.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>

#include <atomic>
#include <FastEPD.h>
#include <lvgl.h>

class FastEpdDisplay final : public tt::hal::display::DisplayDevice {
public:
    struct Configuration final {
        int horizontalResolution;
        int verticalResolution;
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        uint32_t busSpeedHz = 20000000;
        int rotationDegrees = 90;
        bool use4bppGrayscale = false;
        uint32_t fullRefreshEveryNFlushes = 0;
    };

private:
    Configuration configuration;
    std::shared_ptr<tt::Lock> lock;

    lv_display_t* lvglDisplay = nullptr;
    void* lvglBuf1 = nullptr;
    void* lvglBuf2 = nullptr;
    uint32_t lvglBufSize = 0;

    FASTEPD epd;
    bool initialized = false;
    uint32_t flushCount = 0;
    std::atomic_bool forceNextFullRefresh{false};

    static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);

public:
    FastEpdDisplay(Configuration configuration, std::shared_ptr<tt::Lock> lock)
        : configuration(std::move(configuration)), lock(std::move(lock)) {}

    ~FastEpdDisplay() override;

    void requestFullRefresh() override { forceNextFullRefresh.store(true); }

    std::string getName() const override { return "FastEpdDisplay"; }
    std::string getDescription() const override { return "FastEPD (bitbank2) E-Ink display driver"; }

    bool start() override;
    bool stop() override;

    bool supportsLvgl() const override { return true; }
    bool startLvgl() override;
    bool stopLvgl() override;

    lv_display_t* getLvglDisplay() const override { return lvglDisplay; }

    bool supportsDisplayDriver() const override { return false; }
    std::shared_ptr<tt::hal::display::DisplayDriver> getDisplayDriver() override { return nullptr; }

    std::shared_ptr<tt::hal::touch::TouchDevice> getTouchDevice() override {
        return configuration.touch;
    }
};
