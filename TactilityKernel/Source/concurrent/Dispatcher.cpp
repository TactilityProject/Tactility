// SPDX-License-Identifier: Apache-2.0

#include <queue>

#include <Tactility/concurrent/Dispatcher.h>

#include "Tactility/Error.h"

#include <Tactility/Log.h>
#include <Tactility/concurrent/EventGroup.h>
#include <Tactility/concurrent/Mutex.h>
#include <atomic>

#define TAG LOG_TAG("Dispatcher")

static constexpr EventBits_t BACKPRESSURE_WARNING_COUNT = 100U;
static constexpr EventBits_t WAIT_FLAG = 1U;

struct QueuedItem {
    DispatcherCallback callback;
    void* context;
};

struct DispatcherData {
    Mutex mutex = { 0 };
    std::queue<QueuedItem> queue = {};
    EventGroupHandle_t eventGroup = nullptr;
    std::atomic<bool> shutdown{false}; // TODO: Use EventGroup

    DispatcherData() {
        event_group_construct(&eventGroup);
        mutex_construct(&mutex);
    }

    ~DispatcherData() {
        event_group_destruct(&eventGroup);
        mutex_destruct(&mutex);
    }
};

#define dispatcher_data(handle) static_cast<DispatcherData*>(handle)

extern "C" {

DispatcherHandle_t dispatcher_alloc(void) {
    return new DispatcherData();
}

void dispatcher_free(DispatcherHandle_t dispatcher) {
    auto* data = dispatcher_data(dispatcher);
    data->shutdown.store(true, std::memory_order_release);
    mutex_lock(&data->mutex);
    mutex_unlock(&data->mutex);
    delete data;
}

error_t dispatcher_dispatch_timed(DispatcherHandle_t dispatcher, void* callbackContext, DispatcherCallback callback, TickType_t timeout) {
    auto* data = dispatcher_data(dispatcher);

    // Mutate
    if (!mutex_try_lock_timed(&data->mutex, timeout)) {
#ifdef ESP_PLATFORM
        LOG_E(TAG, "Mutex acquisition timeout");
#endif
        return ERROR_TIMEOUT;
    }

    if (data->shutdown.load(std::memory_order_acquire)) {
        mutex_unlock(&data->mutex);
        return ERROR_INVALID_STATE;
    }

    data->queue.push({
        .callback = callback,
        .context = callbackContext
    });

    if (data->queue.size() == BACKPRESSURE_WARNING_COUNT) {
#ifdef ESP_PLATFORM
        LOG_W(TAG, "Backpressure: You're not consuming fast enough (100 queued)");
#endif
    }

    mutex_unlock(&data->mutex);

    if (event_group_set(data->eventGroup, WAIT_FLAG) != ERROR_NONE) {
#ifdef ESP_PLATFORM
        LOG_E(TAG, "Failed to set flag");
#endif
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

error_t dispatcher_consume_timed(DispatcherHandle_t dispatcher, TickType_t timeout) {
    auto* data = dispatcher_data(dispatcher);

    // TODO: keep track of time and consider the timeout input as total timeout

    // Wait for signal
    error_t error = event_group_wait(data->eventGroup, WAIT_FLAG, false, true, nullptr, timeout);
    if (error != ERROR_NONE) {
        if (error == ERROR_TIMEOUT) {
            return ERROR_TIMEOUT;
        } else {
            return ERROR_RESOURCE;
        }
    }

    if (data->shutdown.load(std::memory_order_acquire)) {
        return ERROR_INVALID_STATE;
    }

    // Mutate
    bool processing = true;
    do {
        if (mutex_try_lock_timed(&data->mutex, 10)) {
            if (!data->queue.empty()) {
                // Make a copy, so it's thread-safe when we unlock
                auto entry = data->queue.front();
                data->queue.pop();
                processing = !data->queue.empty();
                // Don't keep lock as callback might be slow and we want to allow dispatch in the meanwhile
                mutex_unlock(&data->mutex);
                entry.callback(entry.context);
            } else {
                processing = false;
                mutex_unlock(&data->mutex);
            }
        } else {
#ifdef ESP_PLATFORM
            LOG_W(TAG, "Mutex acquisition timeout");
#endif
        }

    } while (processing && !data->shutdown.load(std::memory_order_acquire));

    return ERROR_NONE;
}

}