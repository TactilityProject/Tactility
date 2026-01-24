// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Mutex.h"
#include "kernel/Kernel.h"

#include <functional>
#include <list>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#include <cassert>

namespace tt {

/** Publish and subscribe to messages in a thread-safe manner. */
template<typename DataType>
class PubSub final {

    struct Subscription {
        uint64_t id;
        std::function<void(DataType)> callback;
    };

    typedef std::list<Subscription> Subscriptions;
    uint64_t lastId = 0;
    Subscriptions items;
    Mutex mutex;

public:

    typedef void* SubscriptionHandle;

    PubSub() = default;

    ~PubSub() {
        if (!items.empty()) {
#ifdef ESP_PLATFORM
            ESP_LOGW("PubSub", "Destroying with %d active subscriptions", items.size());
#endif
        }

        // Wait for Mutex usage
        if (mutex.lock(kernel::MAX_TICKS)) {
            // TODO: Fix the case where the mutex might be immediately locked after this point and then crashes when deleted
            mutex.unlock();
        }
    }

    /**
     * Start receiving messages at the specified handle (Re-entrable)
     * @param[in] callback
     * @return subscription instance
     */
    SubscriptionHandle subscribe(std::function<void(DataType)> callback) {
        mutex.lock();

        items.push_back({
            .id = (++lastId),
            .callback = std::move(callback)
        });

        mutex.unlock();

        return reinterpret_cast<SubscriptionHandle>(lastId);
    }

    /**
     * Stop receiving messages at the specified handle (Re-entrable)
     * @param[in] subscription
     */
    void unsubscribe(SubscriptionHandle subscription) {
        assert(subscription);

        mutex.lock();

        bool result = false;
        auto id = reinterpret_cast<uint64_t>(subscription);
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (it->id == id) {
                items.erase(it);
                result = true;
                break;
            }
        }

        mutex.unlock();
        assert(result);
    }

    /**
     * Publish something to all subscribers (Re-entrable)
     * @param[in] data the data to publish
     */
    void publish(DataType data) {
        mutex.lock();

        // Iterate over subscribers
        for (auto& it : items) {
            it.callback(data);
        }

        mutex.unlock();
    }
};


} // namespace
