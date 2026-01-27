#pragma once
#ifdef ESP_PLATFORM

#include <Tactility/service/Service.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/Timer.h>

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
    bool stopScreensaverRequested = false;

    // Active screensaver instance
    std::unique_ptr<Screensaver> screensaver;

    // Screensaver auto-off: turn off backlight after 5 minutes of screensaver
    static constexpr int SCREENSAVER_AUTO_OFF_TICKS = 6000;  // 5 minutes at 50ms/tick
    int screensaverActiveCounter = 0;
    bool backlightOff = false;

    static void stopScreensaverCb(lv_event_t* e);
    void activateScreensaver();
    void updateScreensaver();
    void tick();

public:
    bool onStart(ServiceContext& service) override;
    void onStop(ServiceContext& service) override;

    /**
     * Force the screensaver to start immediately, regardless of idle timeout.
     */
    void startScreensaver();

    /**
     * Force the screensaver to stop immediately and restore backlight.
     */
    void stopScreensaver();

    /**
     * Check if the screensaver is currently active.
     * @return true if the screensaver overlay is visible
     */
    bool isScreensaverActive() const;

    /**
     * Reload display settings from storage.
     * Call this when settings have been changed externally.
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
