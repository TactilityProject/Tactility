#ifdef ESP_PLATFORM

#include <Tactility/CoreDefines.h>
#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/KeyboardSettings.h>
#include <Tactility/Timer.h>

#include <tactility/device.h>
#include <tactility/drivers/backlight.h>

namespace tt::service::keyboardidle {

class KeyboardIdleService final : public Service {

    std::unique_ptr<Timer> timer;
    bool keyboardDimmed = false;
    settings::keyboard::KeyboardSettings cachedKeyboardSettings;

    static std::shared_ptr<hal::keyboard::KeyboardDevice> getKeyboard() {
        return hal::findFirstDevice<hal::keyboard::KeyboardDevice>(hal::Device::Type::Keyboard);
    }

    static Device* getKeyboardBacklight() {
        return device_find_by_name("keyboard_backlight");
    }

    void setKeyboardBacklightBrightness(uint8_t brightness) {
        Device* backlight = getKeyboardBacklight();
        if (backlight != nullptr) {
            backlight_set_brightness(backlight, brightness);
        }
    }

    void tick() {
        // Settings are now cached and event-driven (no file I/O in timer callback!)
        // This prevents watchdog timeout from blocking the Timer Service task

        // Query LVGL inactivity once for both checks
        uint32_t inactive_ms = 0;
        if (lvgl::lock(100)) {
            inactive_ms = lv_display_get_inactive_time(nullptr);
            lvgl::unlock();
        }

        // Handle keyboard backlight
        auto keyboard = getKeyboard();
        if (keyboard != nullptr && keyboard->isAttached()) {
            // If timeout disabled, ensure backlight restored if we had dimmed it
            if (!cachedKeyboardSettings.backlightTimeoutEnabled || cachedKeyboardSettings.backlightTimeoutMs == 0) {
                if (keyboardDimmed) {
                    setKeyboardBacklightBrightness(cachedKeyboardSettings.backlightEnabled ? cachedKeyboardSettings.backlightBrightness : 0);
                    keyboardDimmed = false;
                }
            } else {
                if (!keyboardDimmed && inactive_ms >= cachedKeyboardSettings.backlightTimeoutMs) {
                    setKeyboardBacklightBrightness(0);
                    keyboardDimmed = true;
                } else if (keyboardDimmed && inactive_ms < 100) {
                    setKeyboardBacklightBrightness(cachedKeyboardSettings.backlightEnabled ? cachedKeyboardSettings.backlightBrightness : 0);
                    keyboardDimmed = false;
                }
            }
        }
    }

public:
    bool onStart(ServiceContext& service) override {
        // Load settings once at startup and cache them
        // This eliminates file I/O from timer callback (prevents watchdog timeout)
        cachedKeyboardSettings = settings::keyboard::loadOrGetDefault();
        setKeyboardBacklightBrightness(cachedKeyboardSettings.backlightEnabled ? cachedKeyboardSettings.backlightBrightness : 0);

        // Note: Settings changes require service restart to take effect
        // TODO: Add KeyboardSettingsChanged events for dynamic updates
        
        timer = std::make_unique<Timer>(Timer::Type::Periodic, kernel::millisToTicks(250), [this]{ this->tick(); });
        timer->setCallbackPriority(Thread::Priority::Lower);
        timer->start();
        return true;
    }

    void onStop(ServiceContext& service) override {
        if (timer) {
            timer->stop();
            timer = nullptr;
        }
        // Ensure keyboard restored on stop
        auto keyboard = getKeyboard();
        if (keyboard && keyboardDimmed) {
            setKeyboardBacklightBrightness(cachedKeyboardSettings.backlightEnabled ? cachedKeyboardSettings.backlightBrightness : 0);
            keyboardDimmed = false;
        }
    }
};

extern const ServiceManifest manifest = {
    .id = "KeyboardIdle",
    .createService = create<KeyboardIdleService>
};

}

#endif
