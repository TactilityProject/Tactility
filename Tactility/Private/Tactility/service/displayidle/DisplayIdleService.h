#pragma once
#ifdef ESP_PLATFORM

#include <Tactility/service/Service.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/Timer.h>

#include <atomic>
#include <memory>

// Forward declarations
typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;

namespace tt::service::displayidle {

class Screensaver;

class DisplayIdleService final : public Service {

    std::unique_ptr<Timer> timer;
    bool displayDimmed = false;
    settings::display::DisplaySettings cachedDisplaySettings;

    lv_obj_t* screensaverOverlay = nullptr;
    std::atomic<bool> stopScreensaverRequested{false};
    std::atomic<bool> settingsReloadRequested{false};

    // Active screensaver instance
    std::unique_ptr<Screensaver> screensaver;

    // Screensaver auto-off: turn off backlight after 5 minutes of screensaver
    static constexpr uint32_t TICK_INTERVAL_MS = 50;
    static constexpr uint32_t SCREENSAVER_AUTO_OFF_MS = 5 * 60 * 1000;  // 5 minutes
    static constexpr int SCREENSAVER_AUTO_OFF_TICKS = SCREENSAVER_AUTO_OFF_MS / TICK_INTERVAL_MS;
    int screensaverActiveCounter = 0;
    bool backlightOff = false;

    static void stopScreensaverCb(lv_event_t* e);

    /** @pre Caller must hold LVGL lock */
    void activateScreensaver();

    /** @pre Caller must hold LVGL lock */
    void updateScreensaver();

    void tick();

public:
    bool onStart(ServiceContext& service) override;
    void onStop(ServiceContext& service) override;

    /**
     * Force the screensaver to start immediately, regardless of idle timeout.
     * @note Not thread-safe. Call from LVGL/main context only, not from
     *       arbitrary threads while the timer is running.
     */
    void startScreensaver();

    /**
     * Force the screensaver to stop immediately and restore backlight.
     * @note Not thread-safe. Call from LVGL/main context only, not from
     *       arbitrary threads while the timer is running.
     */
    void stopScreensaver();

    /**
     * Check if the screensaver is currently active.
     * @return true if the screensaver overlay is visible
     * @note Not thread-safe. Call from timer thread or LVGL context only.
     */
    bool isScreensaverActive() const;

    /**
     * Request reload of display settings from storage.
     * Thread-safe: can be called from any thread. Actual reload
     * happens on the next timer tick.
     */
    void reloadSettings();
};

/**
 * Find the DisplayIdle service instance.
 * @return shared pointer to the service, or nullptr if not found
 */
std::shared_ptr<DisplayIdleService> findService();

} // namespace tt::service::displayidle

#endif // ESP_PLATFORM
