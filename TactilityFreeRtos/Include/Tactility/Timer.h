// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Thread.h"
#include "freertoscompat/Timers.h"

#include <functional>
#include <memory>

namespace tt {

/**
 * Wrapper class for xTimer functions.
 * @warning Cannot be called from an ISR context except for ::setPendingCallback()
 */
class Timer final {

public:

    enum class Type {
        Once = 0, // Timer triggers once after time has passed
        Periodic = 1 // Timer triggers repeatedly after time has passed
    };

    typedef std::function<void()> Callback;
    typedef void (*PendingCallback)(void* context, uint32_t arg);

private:

    struct TimerHandleDeleter {
        void operator()(TimerHandle_t handleToDelete) const {
            xTimerDelete(handleToDelete, kernel::MAX_TICKS);
        }
    };

    Callback callback;
    std::unique_ptr<std::remove_pointer_t<TimerHandle_t>, TimerHandleDeleter> handle;

    static TimerHandle_t createTimer(Type type, TickType_t ticks, void* timerId, TimerCallbackFunction_t callback) {
        assert(timerId != nullptr);
        assert(callback != nullptr);
        BaseType_t auto_reload = (type == Type::Once) ? pdFALSE : pdTRUE;
        return xTimerCreate(nullptr, ticks, auto_reload, timerId, callback);
    }


    static void onCallback(TimerHandle_t hTimer) {
        auto* timer = static_cast<Timer*>(pvTimerGetTimerID(hTimer));
        if (timer != nullptr) {
            timer->callback();
        }
    }

public:

    /**
     * @param[in] type The timer type
     * @param[in] callback The callback function
     */
    Timer(Type type, TickType_t ticks, Callback callback) :
        callback(callback),
        handle(createTimer(type, ticks, this, onCallback))
    {
        assert(xPortInIsrContext() == pdFALSE);
        assert(handle != nullptr);
    }

    ~Timer() {
        assert(xPortInIsrContext() == pdFALSE);
    }

    /**
     * Start the timer
     * @return success result
     */
    bool start() const {
        assert(xPortInIsrContext() == pdFALSE);
        return xTimerStart(handle.get(), kernel::MAX_TICKS) == pdPASS;
    }

    /** Stop the timer
     * @warning If the timer was just triggered, the callback might still be going on after stop() was called
     * @return success result
     */
    bool stop() const {
        assert(xPortInIsrContext() == pdFALSE);
        return xTimerStop(handle.get(), kernel::MAX_TICKS) == pdPASS;
    }

    /**
     * Set a new interval and reset the timer
     * @param[in] interval The new timer interval
     * @return success result
     */
    bool reset(TickType_t interval) const {
        assert(xPortInIsrContext() == pdFALSE);
        return xTimerChangePeriod(handle.get(), interval, kernel::MAX_TICKS) == pdPASS &&
            xTimerReset(handle.get(), kernel::MAX_TICKS) == pdPASS;
    }

    /**
     * Reset the timer
     * @return success result
     */
    bool reset() const {
        assert(xPortInIsrContext() == pdFALSE);
        return xTimerReset(handle.get(), kernel::MAX_TICKS) == pdPASS;
    }

    /** @return true when the timer is running */
    bool isRunning() const {
        assert(xPortInIsrContext() == pdFALSE);
        return xTimerIsTimerActive(handle.get()) == pdTRUE;
    }

    /** @return the expiry time in ticks */
    TickType_t getExpiryTime() const {
        assert(xPortInIsrContext() == pdFALSE);
        return xTimerGetExpiryTime(handle.get());
    }

    /**
     * Calls xTimerPendFunctionCall internally.
     * @param[in] callback the function to call
     * @param[in] callbackContext the first function argument
     * @param[in] callbackArg the second function argument
     * @param[in] timeout the function timeout (must set to 0 in ISR mode)
     * @return true on success
     */
    bool setPendingCallback(PendingCallback newCallback, void* callbackContext, uint32_t callbackArg, TickType_t timeout) const {
        if (xPortInIsrContext() == pdTRUE) {
            assert(timeout == 0);
            return xTimerPendFunctionCallFromISR(newCallback, callbackContext, callbackArg, nullptr) == pdPASS;
        } else {
            return xTimerPendFunctionCall(newCallback, callbackContext, callbackArg, timeout) == pdPASS;
        }
    }

    /** Set callback priority
     * @param[in] priority The priority
     */
    void setCallbackPriority(Thread::Priority priority) const {
        assert(xPortInIsrContext() == pdFALSE);

        TaskHandle_t task_handle = xTimerGetTimerDaemonTaskHandle();
        assert(task_handle); // Don't call this method before timer task start

        vTaskPrioritySet(task_handle, static_cast<UBaseType_t>(priority));
    }
};

} // namespace
