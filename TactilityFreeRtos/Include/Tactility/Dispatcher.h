// SPDX-License-Identifier: Apache-2.0
/**
* Dispatcher is a thread-safe code execution queue.
*/
#pragma once

#include "EventGroup.h"
#include "Mutex.h"
#include "kernel/Kernel.h"

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#include <functional>
#include <memory>
#include <queue>

namespace tt {

/**
 * A thread-safe way to defer code execution.
 * Generally, one task would dispatch the execution,
 * while the other thread consumes and executes the work.
 */
class Dispatcher final {

    static constexpr auto TAG = "Dispatcher";
    static constexpr EventBits_t BACKPRESSURE_WARNING_COUNT = 100U;
    static constexpr EventBits_t WAIT_FLAG = 1U;

public:

    typedef std::function<void()> Function;

private:

    Mutex mutex;
    std::queue<Function> queue = {};
    EventGroup eventFlag;
    bool shutdown = false;

public:

    explicit Dispatcher() = default;

    ~Dispatcher() {
        shutdown = true;
        if (mutex.lock()) {
		    mutex.unlock();
        }
    }

    /**
     * Queue a function to be consumed elsewhere.
     * @param[in] function the function to execute elsewhere
     * @param[in] timeout lock acquisition timeout
     * @return true if dispatching was successful (timeout not reached)
     */
    bool dispatch(Function function, TickType_t timeout = kernel::MAX_TICKS) {
        // Mutate
        if (!mutex.lock(timeout)) {
#ifdef ESP_PLATFORM
            ESP_LOGE(TAG, "Mutex acquisition timeout");
#endif
            return false;
        }

        if (shutdown) {
            mutex.unlock();
            return false;
        }

        queue.push(std::move(function));
        if (queue.size() == BACKPRESSURE_WARNING_COUNT) {
#ifdef ESP_PLATFORM
            ESP_LOGW(TAG, "Backpressure: You're not consuming fast enough (100 queued)");
#endif
        }
        mutex.unlock();
        if (!eventFlag.set(WAIT_FLAG)) {
#ifdef ESP_PLATFORM
            ESP_LOGE(TAG, "Failed to set flag");
#endif
        }
        return true;
    }

    /**
     * Consume 1 or more dispatched functions (if any) until the queue is empty.
     * @warning The timeout is only the wait time before consuming the message! It is not a limit to the total execution time when calling this method.
     * @param[in] timeout the ticks to wait for a message
     * @return the amount of messages that were consumed
     */
    uint32_t consume(TickType_t timeout = kernel::MAX_TICKS) {
        // Wait for signal
        if (!eventFlag.wait(WAIT_FLAG, false, true, nullptr, timeout)) {
            return 0;
        }

        if (shutdown) {
            return 0;
        }

        // Mutate
        bool processing = true;
        uint32_t consumed = 0;
        do {
            if (mutex.lock(10)) {
                if (!queue.empty()) {
                    auto function = queue.front();
                    queue.pop();
                    consumed++;
                    processing = !queue.empty();
                    // Don't keep lock as callback might be slow
                    mutex.unlock();
                    function();
                } else {
                    processing = false;
                    mutex.unlock();
                }
            } else {
#ifdef ESP_PLATFORM
                ESP_LOGW(TAG, "Mutex acquisition timeout");
#endif
            }

        } while (processing && !shutdown);

        return consumed;
    }
};

} // namespace
