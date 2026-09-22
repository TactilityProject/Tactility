#pragma once

#include <Tactility/service/Service.h>
#include <Tactility/service/autorotate/OrientationMath.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/Timer.h>

#include <atomic>
#include <memory>

namespace tt::service::autorotate {

class AutoRotateService final : public Service {
    std::unique_ptr<Timer> timer;
    OrientationMath orientationMath;
    settings::display::DisplaySettings cachedDisplaySettings;
    std::atomic<bool> settingsReloadRequested{false};

    static constexpr uint32_t TICK_INTERVAL_MS = 100; // ~10 Hz IMU poll rate

    void tick();

public:
    bool onStart(ServiceContext& service) override;
    void onStop(ServiceContext& service) override;

    /**
     * Request reload of display settings from storage.
     * Thread-safe: can be called from any thread. Actual reload happens on the next timer tick.
     */
    void reloadSettings();
};

/**
 * Find the AutoRotate service instance.
 * @return shared pointer to the service, or nullptr if not found
 */
std::shared_ptr<AutoRotateService> findService();

} // namespace tt::service::autorotate
