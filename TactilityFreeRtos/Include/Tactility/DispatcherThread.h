// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Dispatcher.h"
#include "Thread.h"

namespace tt {

/** Starts a Thread to process dispatched messages */
class DispatcherThread final {

    Dispatcher dispatcher;
    std::unique_ptr<Thread> thread;
    bool interruptThread = true;

    int32_t threadMain() {
        do {
            /**
             * If this value is too high (e.g. 1 second) then the dispatcher destroys too slowly when the simulator exits.
             * This causes the problems with other services doing an update (e.g. Statusbar) and calling into destroyed mutex in the global scope.
             */
            dispatcher.consume(100 / portTICK_PERIOD_MS);
        } while (!interruptThread);

        return 0;
    }

public:

    explicit DispatcherThread(const std::string& threadName, size_t threadStackSize = 4096) {
        thread = std::make_unique<Thread>(
            threadName,
            threadStackSize,
            [this] {
                return threadMain();
            }
        );
    }

    ~DispatcherThread() {
        if (thread->getState() != Thread::State::Stopped) {
            stop();
        }
    }

    /**
     * Dispatch a message.
     */
    bool dispatch(const Dispatcher::Function& function, TickType_t timeout = kernel::MAX_TICKS) {
        return dispatcher.dispatch(function, timeout);
    }

    /** Start the thread (blocking). */
    void start() {
        interruptThread = false;
        thread->start();
    }

    /** Stop the thread (blocking). */
    void stop() {
        interruptThread = true;
        thread->join();
    }

    /** @return true of the thread is started */
    bool isStarted() const { return thread != nullptr && !interruptThread; }
};

}