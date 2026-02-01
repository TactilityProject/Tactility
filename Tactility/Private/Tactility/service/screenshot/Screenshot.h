#pragma once

#include "Tactility/TactilityConfig.h"

#if TT_FEATURE_SCREENSHOT_ENABLED

#include "ScreenshotTask.h"

#include <Tactility/Mutex.h>
#include <Tactility/service/Service.h>

namespace tt::service::screenshot {

enum class Mode {
    None,
    Timed,
    Apps
};

class ScreenshotService final : public Service {

    Mutex mutex;
    std::unique_ptr<ScreenshotTask> task;
    Mode mode = Mode::None;

public:

    bool onStart(ServiceContext& serviceContext) override;

    bool isTaskStarted();

    /** The state of the service. */
    Mode getMode() const;

    /** @brief Start taking screenshot whenever an app is started
     * @param[in] path the path to store the screenshots at
     */
    void startApps(const std::string& path);

    /** @brief Start taking screenshots after a certain delay
     * @param[in] path the path to store the screenshots at
     * @param[in] delayInSeconds the delay before starting (and between successive screenshots)
     * @param[in] amount 0 = indefinite, >0 for a specific
     */
    void startTimed(const std::string& path, uint8_t delayInSeconds, uint8_t amount);

    /** @brief Stop taking screenshots */
    void stop();
};

std::shared_ptr<ScreenshotService> optScreenshotService();

} // namespace

#endif
