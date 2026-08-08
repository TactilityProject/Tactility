#pragma once

#include "./View.h"
#include "./State.h"

#include <Tactility/Mutex.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <tactility/drivers/bluetooth.h>

#include <atomic>
#include <memory>

namespace tt::app::btmanage {

struct Context {
    uint32_t appInstanceId;
    Mutex mutex;
    Bindings bindings {};
    State state;
    View view = View(&bindings, &state);
    Device* btDevice = nullptr;
    bool callbackRegistered = false;

    // Bumped right before the BT event callback is unregistered at the end of appMain(), to
    // invalidate any BT event already dispatched to the main task for this instance. Kept in
    // its own heap allocation, independent of Context's (stack-local) lifetime, so a dispatched
    // callback can check it without touching a possibly already-destroyed Context.
    std::shared_ptr<std::atomic<int>> generation = std::make_shared<std::atomic<int>>(0);

    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }
};

void onBtEvent(Context* ctx, const struct BtEvent& event);
void requestViewUpdate(Context* ctx);

// Re-attempts registering the device event callback. Needed because the BLE driver only
// allocates its callback list while the device is started/on: a registration attempted while
// the radio is off silently no-ops, so this must be called again right after a successful
// bluetooth::start(). Idempotent: no-ops if already registered for this device.
void registerDeviceCallback(Context* ctx, Device* dev);

// Call after bluetooth::stop(): the driver frees its callback list on stop, so the
// registration state must be cleared here too, without touching the (now-dangling) driver-side
// list.
void forgetCallbackRegistration(Context* ctx);

} // namespace tt::app::btmanage
