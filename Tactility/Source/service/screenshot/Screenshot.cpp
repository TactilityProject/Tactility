#include <Tactility/TactilityConfig.h>

#if TT_FEATURE_SCREENSHOT_ENABLED

#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/service/screenshot/Screenshot.h>
#include <Tactility/service/ServiceRegistration.h>

#include <lvgl.h>
#include <memory>

namespace tt::service::screenshot {

static const auto LOGGER = Logger("ScreenshotService");

extern const ServiceManifest manifest;

std::shared_ptr<ScreenshotService> optScreenshotService() {
    return service::findServiceById<ScreenshotService>(manifest.id);
}

void ScreenshotService::startApps(const std::string& path) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    if (task == nullptr || task->isFinished()) {
        task = std::make_unique<ScreenshotTask>();
        mode = Mode::Apps;
        task->startApps(path);
    } else {
        LOGGER.warn("Screenshot task already running");
    }
}

void ScreenshotService::startTimed(const std::string& path, uint8_t delayInSeconds, uint8_t amount) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    if (task == nullptr || task->isFinished()) {
        task = std::make_unique<ScreenshotTask>();
        mode = Mode::Timed;
        task->startTimed(path, delayInSeconds, amount);
    } else {
        LOGGER.warn("Screenshot task already running");
    }
}

bool ScreenshotService::onStart(ServiceContext& serviceContext) {
    if (lv_screen_active() == nullptr) {
        LOGGER.error("No display found");
        return false;
    }

    return true;
}

void ScreenshotService::stop() {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    if (task != nullptr) {
        task = nullptr;
        mode = Mode::None;
    } else {
        LOGGER.warn("Screenshot task not running");
    }
}

Mode ScreenshotService::getMode() const {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return Mode::None;
    }

    return mode;
}

bool ScreenshotService::isTaskStarted() {
    auto* current_task = task.get();
    if (current_task == nullptr) {
        return false;
    } else {
        return !current_task->isFinished();
    }
}

extern const ServiceManifest manifest = {
    .id = "Screenshot",
    .createService = create<ScreenshotService>
};

} // namespace

#endif