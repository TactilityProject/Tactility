#pragma once

#include "./View.h"
#include "./State.h"

#include <Tactility/Mutex.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/drivers/bluetooth.h>

#include <atomic>

namespace tt::app::btmanage {

struct Context {
    uint32_t appInstanceId;
    Mutex mutex;
    Bindings bindings {};
    State state;
    View view = View(&bindings, &state);
    Device* btDevice = nullptr;
    BtEventSubscription btEventSub {};

    // Requested from LVGL callbacks, handled on the app task: -1 none, 0 turn off, 1 turn on
    TaskEventGroup* eventGroup = nullptr;
    uint32_t requestBit = 0;
    std::atomic<int> radioRequest = -1;

    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }
};

void onBtEvent(Context* ctx, const struct BtEvent& event);
void requestViewUpdate(Context* ctx);

} // namespace tt::app::btmanage
