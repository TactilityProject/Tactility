#ifdef ESP_PLATFORM

#include <Tactility/service/displayidle/DisplayIdleService.h>

#include "Screensaver.h"
#include "BouncingBallsScreensaver.h"
#include "MatrixRainScreensaver.h"
#include "MystifyScreensaver.h"

#include <Tactility/Logger.h>
#include <Tactility/CoreDefines.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <cstdlib>
#include <ctime>

namespace tt::service::displayidle {

static const auto LOGGER = Logger("DisplayIdle");

constexpr uint32_t kWakeActivityThresholdMs = 100;

static std::shared_ptr<hal::display::DisplayDevice> getDisplay() {
    return hal::findFirstDevice<hal::display::DisplayDevice>(hal::Device::Type::Display);
}

void DisplayIdleService::stopScreensaverCb(lv_event_t* e) {
    auto* self = static_cast<DisplayIdleService*>(lv_event_get_user_data(e));
    lv_event_stop_bubbling(e);
    self->stopScreensaverRequested.store(true, std::memory_order_release);
    lv_display_trigger_activity(nullptr);
}

void DisplayIdleService::stopScreensaver() {
    if (!lvgl::lock(100)) {
        // Lock failed - keep flag set to retry on next tick
        return;
    }

    if (screensaverOverlay) {
        if (screensaver) {
            screensaver->stop();
            screensaver.reset();
        }
        lv_obj_delete(screensaverOverlay);
        screensaverOverlay = nullptr;
    }
    lvgl::unlock();
    stopScreensaverRequested.store(false, std::memory_order_relaxed);

    // Reset auto-off state
    screensaverActiveCounter = 0;
    backlightOff = false;

    // Restore backlight if display was dimmed
    auto display = getDisplay();
    if (display && displayDimmed) {
        display->setBacklightDuty(cachedDisplaySettings.backlightDuty);
    }
    displayDimmed = false;
}

void DisplayIdleService::activateScreensaver() {
    lv_obj_t* top = lv_layer_top();

    if (screensaverOverlay != nullptr) return;

    // Reset auto-off counter when starting screensaver
    screensaverActiveCounter = 0;
    backlightOff = false;

    lv_coord_t screenW = lv_display_get_horizontal_resolution(nullptr);
    lv_coord_t screenH = lv_display_get_vertical_resolution(nullptr);

    // Black background overlay
    screensaverOverlay = lv_obj_create(top);
    lv_obj_remove_style_all(screensaverOverlay);
    lv_obj_set_size(screensaverOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(screensaverOverlay, 0, 0);
    lv_obj_set_style_bg_color(screensaverOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screensaverOverlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(screensaverOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screensaverOverlay, stopScreensaverCb, LV_EVENT_CLICKED, this);

    // Create and start the screensaver based on settings
    switch (cachedDisplaySettings.screensaverType) {
        case settings::display::ScreensaverType::Mystify:
            screensaver = std::make_unique<MystifyScreensaver>();
            break;
        case settings::display::ScreensaverType::BouncingBalls:
            screensaver = std::make_unique<BouncingBallsScreensaver>();
            break;
        case settings::display::ScreensaverType::MatrixRain:
            screensaver = std::make_unique<MatrixRainScreensaver>();
            break;
        case settings::display::ScreensaverType::None:
        default:
            // Just black screen, no animated screensaver
            screensaver = nullptr;
            break;
    }

    if (screensaver) {
        screensaver->start(screensaverOverlay, screenW, screenH);
    }
}

void DisplayIdleService::updateScreensaver() {
    if (screensaver) {
        lv_coord_t screenW = lv_display_get_horizontal_resolution(nullptr);
        lv_coord_t screenH = lv_display_get_vertical_resolution(nullptr);
        screensaver->update(screenW, screenH);
    }
}

void DisplayIdleService::tick() {
    if (lv_display_get_default() == nullptr) {
        return;
    }

    // Check for settings reload request (thread-safe)
    if (settingsReloadRequested.exchange(false, std::memory_order_acquire)) {
        cachedDisplaySettings = settings::display::loadOrGetDefault();
    }

    uint32_t inactive_ms = 0;
    if (lvgl::lock(100)) {
        inactive_ms = lv_display_get_inactive_time(nullptr);

        // Only update if not stopping (prevents lag on touch)
        if (displayDimmed && screensaverOverlay && !stopScreensaverRequested.load(std::memory_order_acquire)) {
            // Check if screensaver should auto-off after 5 minutes
            if (!backlightOff) {
                screensaverActiveCounter++;
                if (screensaverActiveCounter >= SCREENSAVER_AUTO_OFF_TICKS) {
                    // Stop screensaver animation and turn off backlight
                    if (screensaver) {
                        screensaver->stop();
                        screensaver.reset();
                    }
                    auto display = getDisplay();
                    if (display) {
                        display->setBacklightDuty(0);
                    }
                    backlightOff = true;
                } else {
                    updateScreensaver();
                }
            }
        }

        lvgl::unlock();
    } else {
        return;
    }

    // Check stop request early for faster response
    if (stopScreensaverRequested.load(std::memory_order_acquire)) {
        stopScreensaver();
        return;
    }

    auto display = getDisplay();
    if (display != nullptr && display->supportsBacklightDuty()) {
        if (!cachedDisplaySettings.backlightTimeoutEnabled || cachedDisplaySettings.backlightTimeoutMs == 0) {
            if (displayDimmed) {
                display->setBacklightDuty(cachedDisplaySettings.backlightDuty);
                displayDimmed = false;
            }
        } else {
            if (!displayDimmed && inactive_ms >= cachedDisplaySettings.backlightTimeoutMs) {
                if (!lvgl::lock(100)) {
                    return; // Retry on next tick
                }
                activateScreensaver();
                lvgl::unlock();
                // Turn off backlight for "None" screensaver (just black screen)
                if (cachedDisplaySettings.screensaverType == settings::display::ScreensaverType::None) {
                    display->setBacklightDuty(0);
                }
                displayDimmed = true;
            } else if (displayDimmed && (inactive_ms < kWakeActivityThresholdMs)) {
                stopScreensaver();
            }
        }
    }
}

bool DisplayIdleService::onStart(ServiceContext& service) {
    // Seed random number generator for varied screensaver patterns
    srand(static_cast<unsigned int>(time(nullptr)));

    cachedDisplaySettings = settings::display::loadOrGetDefault();

    timer = std::make_unique<Timer>(Timer::Type::Periodic, kernel::millisToTicks(TICK_INTERVAL_MS), [this]{ this->tick(); });
    timer->setCallbackPriority(Thread::Priority::Lower);
    timer->start();
    return true;
}

void DisplayIdleService::onStop(ServiceContext& service) {
    if (timer) {
        timer->stop();
        timer = nullptr;
    }
    if (screensaverOverlay) {
        // Retry screensaver cleanup during shutdown
        constexpr int maxRetries = 5;
        for (int i = 0; i < maxRetries && screensaverOverlay; ++i) {
            stopScreensaver();
            if (screensaverOverlay && i < maxRetries - 1) {
                kernel::delayMillis(50); // Brief delay before retry
            }
        }
        if (screensaverOverlay) {
            LOGGER.warn("Failed to stop screensaver during shutdown - potential resource leak");
        }
    }
    screensaver.reset();
}

void DisplayIdleService::startScreensaver() {
    if (!lvgl::lock(100)) {
        return;
    }

    // Reload settings to get current screensaver type
    // Note: This is safe because we hold the LVGL lock which serializes with tick()
    cachedDisplaySettings = settings::display::loadOrGetDefault();

    activateScreensaver();
    lvgl::unlock();

    // Turn off backlight for "None" screensaver
    auto display = getDisplay();
    if (display && cachedDisplaySettings.screensaverType == settings::display::ScreensaverType::None) {
        display->setBacklightDuty(0);
    }
    displayDimmed = true;
}

bool DisplayIdleService::isScreensaverActive() const {
    return screensaverOverlay != nullptr;
}

void DisplayIdleService::reloadSettings() {
    // Set flag for thread-safe reload - actual reload happens in tick()
    settingsReloadRequested.store(true, std::memory_order_release);
}

std::shared_ptr<DisplayIdleService> findService() {
    return std::static_pointer_cast<DisplayIdleService>(
        findServiceById("DisplayIdle")
    );
}

extern const ServiceManifest manifest = {
    .id = "DisplayIdle",
    .createService = create<DisplayIdleService>
};

}

#endif // ESP_PLATFORM
