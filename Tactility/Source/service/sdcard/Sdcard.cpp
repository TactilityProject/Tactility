#include <Tactility/LogMessages.h>
#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <Tactility/Paths.h>
#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceRegistration.h>

namespace tt::service::sdcard {

static const auto LOGGER = Logger("SdcardService");

extern const ServiceManifest manifest;

class SdCardService final : public Service {

    Mutex mutex;
    std::unique_ptr<Timer> updateTimer;
    bool lastMountedState = false;

    bool lock(TickType_t timeout) const {
        return mutex.lock(timeout);
    }

    void unlock() const {
        mutex.unlock();
    }

    void update() {
        // TODO: Support multiple SD cards
        auto* file_system = findFirstSdcardFileSystem();

        if (lock(50)) {
            auto is_mounted = file_system_is_mounted(file_system);
            if (is_mounted != lastMountedState) {
                lastMountedState = is_mounted;
            }
            unlock();
        } else {
            LOGGER.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        }
    }

public:

    bool onStart(ServiceContext& serviceContext) override {
        auto* sdcard_fs = findFirstSdcardFileSystem();
        if (sdcard_fs == nullptr) {
            LOGGER.warn("No SD card device found - not starting Service");
            return false;
        }

        auto service = findServiceById<SdCardService>(manifest.id);
        updateTimer = std::make_unique<Timer>(Timer::Type::Periodic, 1000, [service] {
            service->update();
        });

        // We want to try and scan more often in case of startup or scan lock failure
        updateTimer->start();

        return true;
    }

    void onStop(ServiceContext& serviceContext) override {
        if (updateTimer != nullptr) {
            // Stop thread
            updateTimer->stop();
            updateTimer = nullptr;
        }
    }
};

extern const ServiceManifest manifest = {
    .id = "sdcard",
    .createService = create<SdCardService>
};

} // namespace
