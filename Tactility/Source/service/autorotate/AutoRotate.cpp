#include <Tactility/service/autorotate/AutoRotateService.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <tactility/delay.h>
#include <tactility/drivers/imu.h>

#include <lvgl/lvgl.h>

namespace tt::service::autorotate {

using settings::display::Orientation;

void AutoRotateService::tick() {
    if (settingsReloadRequested.load(std::memory_order_acquire)) {
        auto reloaded = settings::display::loadOrGetDefault();
        if (!lvgl_try_lock(100)) {
            return; // stays pending - keeps orientationMath in sync with cachedDisplaySettings
        }
        cachedDisplaySettings = reloaded;
        orientationMath.reset(settings::display::toLvglDisplayRotation(cachedDisplaySettings.orientation));
        lvgl_unlock();
        settingsReloadRequested.store(false, std::memory_order_release);
    }

    if (!cachedDisplaySettings.autoRotateEnabled) {
        return;
    }

    ::Device* imu;
    if (device_get_first_active_by_type(&IMU_TYPE, &imu) != ERROR_NONE) {
        return;
    }

    ImuAccelData accel;
    error_t result = imu_read_accel(imu, &accel);
    device_put(imu);
    if (result != ERROR_NONE) {
        return;
    }

    if (!lvgl_try_lock(100)) {
        return; // Retry on next tick
    }

    int mount_quarter_turns = static_cast<int>(cachedDisplaySettings.autoRotateMountRotation);
    lv_display_rotation_t rotation = orientationMath.update(accel.ax, accel.ay, mount_quarter_turns);
    Orientation orientation = settings::display::fromLvglDisplayRotation(rotation);
    if (orientation == cachedDisplaySettings.orientation) {
        lvgl_unlock();
        return;
    }

    lv_display_set_rotation(lv_display_get_default(), rotation);
    lvgl_unlock();

    // Re-load rather than write back the cached snapshot, so a concurrent Display app save isn't clobbered.
    auto settings_to_save = settings::display::loadOrGetDefault();
    settings_to_save.orientation = orientation;
    cachedDisplaySettings.orientation = orientation;
    settings::display::save(settings_to_save);
}

bool AutoRotateService::onStart(ServiceContext& service) {
    cachedDisplaySettings = settings::display::loadOrGetDefault();
    if (lvgl_try_lock(100)) {
        orientationMath.reset(settings::display::toLvglDisplayRotation(cachedDisplaySettings.orientation));
        lvgl_unlock();
    } else {
        // Lock failed - defer the sync to tick()'s reload path instead of a mismatched baseline.
        settingsReloadRequested.store(true, std::memory_order_release);
    }

    timer = std::make_unique<Timer>(Timer::Type::Periodic, millis_to_ticks(TICK_INTERVAL_MS), [this]{ this->tick(); });
    timer->setCallbackPriority(Thread::Priority::Lower);
    timer->start();
    return true;
}

void AutoRotateService::onStop(ServiceContext& service) {
    if (timer) {
        timer->stop();
        timer = nullptr;
    }
}

void AutoRotateService::reloadSettings() {
    settingsReloadRequested.store(true, std::memory_order_release);
}

std::shared_ptr<AutoRotateService> findService() {
    return std::static_pointer_cast<AutoRotateService>(
        findServiceById("tactility.autorotate")
    );
}

extern const ServiceManifest manifest = {
    .id = "tactility.autorotate",
    .createService = create<AutoRotateService>
};

} // namespace tt::service::autorotate
