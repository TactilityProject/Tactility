#include "Tactility/lvgl/LvglSync.h"

#include <Tactility/Mutex.h>
#include <tactility/lvgl_module.h>

namespace tt::lvgl {

bool lock(TickType_t timeout) {
    return lvgl_try_lock(timeout);
}

void unlock() {
    lvgl_unlock();
}

class LvglSync : public Lock {
public:
    ~LvglSync() override = default;

    bool lock(TickType_t timeoutTicks) const override {
        return lvgl_try_lock(timeoutTicks);
    }

    void unlock() const override {
        lvgl_unlock();
    }
};

static std::shared_ptr<Lock> lvglSync = std::make_shared<LvglSync>();

std::shared_ptr<Lock> getSyncLock() {
    return lvglSync;
}

} // namespace
