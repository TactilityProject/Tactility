// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <vector>

#include <tactility/device_listener.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device_listener_internal.h>

struct DeviceListenerLedger {
    std::vector<DeviceEventListener> listeners;
    Mutex mutex {};

    DeviceListenerLedger() { mutex_construct(&mutex); }
    ~DeviceListenerLedger() { mutex_destruct(&mutex); }

    void lock() { mutex_lock(&mutex); }
    void unlock() { mutex_unlock(&mutex); }
};

static DeviceListenerLedger ledger;

extern "C" {

void device_listener_add(DeviceListenerCallback callback, void* context) {
    ledger.lock();
    ledger.listeners.push_back(DeviceEventListener{ *callback, context });
    ledger.unlock();
}

void device_listener_remove(DeviceListenerCallback callback) {
    ledger.lock();
    const auto iterator = std::ranges::find_if(ledger.listeners, [callback](const DeviceEventListener& listener) {
        return listener.callback == *callback;
    });
    if (iterator != ledger.listeners.end()) {
        ledger.listeners.erase(iterator);
    }
    ledger.unlock();
}

void device_listener_notify(Device* dev, DeviceEvent event) {
    // Copy the listener list under the lock, then invoke callbacks after unlocking: a listener
    // calling device_listener_add/remove from within its own callback would otherwise deadlock
    // against this same (non-recursive) mutex, and a slow listener would block every other
    // thread's add/remove for the duration of this notification.
    ledger.lock();
    const std::vector<DeviceEventListener> listeners_copy = ledger.listeners;
    ledger.unlock();

    for (const auto& listener : listeners_copy) {
        listener.callback(dev, event, listener.callback_context);
    }
}

} // extern "C"
