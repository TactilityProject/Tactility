#include <Tactility/TactilityConfig.h>

#if TT_FEATURE_SCREENSHOT_ENABLED

#include <Tactility/LogMessages.h>
#include <Tactility/CpuAffinity.h>
#include <Tactility/TactilityCore.h>
#include <Tactility/service/screenshot/ScreenshotTask.h>

#include <app/manager.h>

#include <tactility/delay.h>
#include <tactility/log.h>

#include <lvgl/lvgl.h>

#include <lv_screenshot.h>

#include <format>

namespace tt::service::screenshot {

constexpr auto* TAG = "ScreenshotTask";

ScreenshotTask::~ScreenshotTask() {
    if (thread) {
        stop();
    }
}

bool ScreenshotTask::isInterrupted() {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOG_W(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return true;
    }
    return interrupted;
}

bool ScreenshotTask::isFinished() {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOG_W(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return false;
    }
    return finished;
}

void ScreenshotTask::setFinished() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    finished = true;
}

static void makeScreenshot(const std::string& filename) {
    if (lvgl_try_lock(50 / portTICK_PERIOD_MS)) {
        if (lv_screenshot_create(lv_scr_act(), LV_100ASK_SCREENSHOT_SV_PNG, filename.c_str())) {
            LOG_I(TAG, "Screenshot saved to %s", filename.c_str());
        } else {
            LOG_E(TAG, "Screenshot not saved to %s", filename.c_str());
        }
        lvgl_unlock();
    } else {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
    }
}

void ScreenshotTask::taskMain() {
    uint8_t screenshots_taken = 0;
    uint32_t last_app_instance_id = 0;

    while (!isInterrupted()) {
        if (work.type == TASK_WORK_TYPE_DELAY) {
            // Splitting up the delays makes it easier to stop the service
            for (int i = 0; i < (work.delay_in_seconds * 10) && !isInterrupted(); ++i){
                delay_millis(100);
            }

            if (!isInterrupted()) {
                screenshots_taken++;
                std::string filename = std::format("{}/screenshot-{}.png", work.path, screenshots_taken);
                makeScreenshot(filename);

                if (work.amount > 0 && screenshots_taken >= work.amount) {
                    break; // Interrupted loop
                }
            }
        } else if (work.type == TASK_WORK_TYPE_APPS) {
            AppInstanceId app_instance_id = 0;
            bool has_topmost = app_manager_get_topmost_instance_id(&app_instance_id) == ERROR_NONE;
            if (has_topmost && app_instance_id != last_app_instance_id) {
                delay_millis(100);
                last_app_instance_id = app_instance_id;
                auto filename = std::format("{}/screenshot-{}.png", work.path, app_instance_id);
                makeScreenshot(filename);
            }
            // Ensure the LVGL widgets are rendered as the app just started
            delay_millis(250);
        }
    }

    setFinished();
}

void ScreenshotTask::taskStart() {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    check(thread == nullptr);
    thread = new Thread(
        "screenshot",
        8192,
        [this] {
            this->taskMain();
            return 0;
        },
        getCpuAffinityConfiguration().graphics
    );
    thread->start();
}

void ScreenshotTask::startApps(const std::string& path) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    if (thread == nullptr) {
        interrupted = false;
        work.type = TASK_WORK_TYPE_APPS;
        work.path = path;
        taskStart();
    } else {
        LOG_E(TAG, "Task was already running");
    }
}

void ScreenshotTask::startTimed(const std::string& path, uint8_t delay_in_seconds, uint8_t amount) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    if (thread == nullptr) {
        interrupted = false;
        work.type = TASK_WORK_TYPE_DELAY;
        work.delay_in_seconds = delay_in_seconds;
        work.amount = amount;
        work.path = path;
        taskStart();
    } else {
        LOG_E(TAG, "Task was already running");
    }
}

void ScreenshotTask::stop() {
    if (thread != nullptr) {
        if (mutex.lock(50 / portTICK_PERIOD_MS)) {
            interrupted = true;
            mutex.unlock();
        }

        thread->join();

        if (mutex.lock(50 / portTICK_PERIOD_MS)) {
            delete thread;
            thread = nullptr;
            mutex.unlock();
        }
    }
}

} // namespace

#endif
