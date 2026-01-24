// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "freertoscompat/Task.h"
#include "kernel/Kernel.h"
#include "Mutex.h"

#include <cassert>
#include <functional>
#include <memory>
#include <string>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

namespace tt {

class Thread final {

    static constexpr size_t LOCAL_STORAGE_SELF_POINTER_INDEX = 0;

public:

    enum class State{
        Stopped,
        Starting,
        Running,
    };

    /** ThreadPriority */
    enum class Priority : UBaseType_t {
        None = 0U,
        Idle = 1U,
        Lower = 2U,
        Low = 3U,
        Normal = 4U,
        High = 5U,
        Higher = 6U,
        Critical = 7U
    };

    typedef std::function<int32_t()> MainFunction;

    typedef void (*StateCallback)(State state, void* context);

private:

    static constexpr auto TAG = "Thread";

    static_assert(static_cast<UBaseType_t>(Priority::Critical) < configMAX_PRIORITIES, "Highest thread priority is higher than max priority");

    static void mainBody(void* context) {
        assert(context != nullptr);
        auto* thread = static_cast<Thread*>(context);

        // Save Thread instance pointer to task local storage
        assert(pvTaskGetThreadLocalStoragePointer(nullptr, LOCAL_STORAGE_SELF_POINTER_INDEX) == nullptr);
        vTaskSetThreadLocalStoragePointer(nullptr, LOCAL_STORAGE_SELF_POINTER_INDEX, thread);

#ifdef ESP_PLATFORM
        ESP_LOGI(TAG, "Starting %s", thread->name.c_str());
#endif
        assert(thread->state == State::Starting);
        thread->setState(State::Running);
        thread->callbackResult = thread->mainFunction();
        assert(thread->state == State::Running);
        thread->setState(State::Stopped);
#ifdef ESP_PLATFORM
        ESP_LOGI(TAG, "Stopped %s", thread->name.c_str());
#endif

        vTaskSetThreadLocalStoragePointer(nullptr, 0, nullptr);
        thread->taskHandle = nullptr;

        vTaskDelete(nullptr);
    }

    TaskHandle_t taskHandle = nullptr;
    State state = State::Stopped;
    MainFunction mainFunction;
    int32_t callbackResult = 0;
    StateCallback stateCallback = nullptr;
    void* stateCallbackContext = nullptr;
    std::string name = {};
    Priority priority = Priority::Normal;
    Mutex mutex;
    configSTACK_DEPTH_TYPE stackSize = 0;
    portBASE_TYPE affinity = -1;

    void setState(State newState) {
        mutex.lock();
        state = newState;
        if (stateCallback) {
            stateCallback(state, stateCallbackContext);
        }
        mutex.unlock();
    }

public:

    Thread() = default;

    Thread(
        std::string name,
        configSTACK_DEPTH_TYPE stackSize,
        MainFunction function,
        portBASE_TYPE affinity = -1
    ) :
        mainFunction(function),
        name(std::move(name)),
        stackSize(stackSize),
        affinity(affinity)
    {}

    /** @warning If thread is running, you just call join() first */
    ~Thread() {
        assert(state == State::Stopped);
        assert(taskHandle == nullptr);
    }

    void setName(std::string newName) {
        mutex.lock();
        assert(state == State::Stopped);
        name = std::move(newName);
        mutex.unlock();
    }

    void setStackSize(size_t newStackSize) {
        mutex.lock();
        assert(state == State::Stopped);
        assert(newStackSize % 4 == 0);
        stackSize = newStackSize;
        mutex.unlock();
    }

    void setAffinity(portBASE_TYPE newAffinity) {
        mutex.lock();
        assert(state == State::Stopped);
        affinity = newAffinity;
        mutex.unlock();
    }

    void setMainFunction(MainFunction function) {
        mutex.lock();
        assert(state == State::Stopped);
        mainFunction = function;
        mutex.unlock();
    }

    void setPriority(Priority newPriority) {
        mutex.lock();
        assert(state == State::Stopped);
        priority = newPriority;
        mutex.unlock();
    }

    void setStateCallback(StateCallback callback, _Nullable void* callbackContext = nullptr) {
        mutex.lock();
        assert(state == State::Stopped);
        stateCallback = callback;
        stateCallbackContext = callbackContext;
        mutex.unlock();
    }

    State getState() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return state;
    }

    void start() {
        mutex.lock();
        assert(mainFunction);
        assert(state == State::Stopped);
        assert(stackSize > 0 && stackSize < (UINT16_MAX * sizeof(StackType_t)));
        mutex.unlock();

        setState(State::Starting);

        mutex.lock();
        uint32_t stack_depth = stackSize / sizeof(StackType_t);
        mutex.unlock();

        BaseType_t result;
        if (affinity != -1) {
#ifdef ESP_PLATFORM
            result = xTaskCreatePinnedToCore(
                mainBody,
                name.c_str(),
                stack_depth,
                this,
                static_cast<UBaseType_t>(priority),
                &taskHandle,
                affinity
            );
#else
            // Pinned tasks are not supported by current FreeRTOS platform - creating regular one
            result = xTaskCreate(
                mainBody,
                name.c_str(),
                stack_depth,
                this,
                static_cast<UBaseType_t>(priority),
                &taskHandle
            );
#endif
        } else {
            result = xTaskCreate(
                mainBody,
                name.c_str(),
                stack_depth,
                this,
                static_cast<UBaseType_t>(priority),
                &taskHandle
            );
        }

        assert(result == pdPASS);
        assert(taskHandle != nullptr || getState() == State::Stopped);
    }

    /**
     * @warning If this blocks forever, it might be because of the Thread, but it could also be because another task is blocking the CPU.
     */
    bool join(TickType_t timeout = kernel::MAX_TICKS, TickType_t pollInterval = 10) {
        assert(getCurrent() != this);

        TickType_t start_ticks = kernel::getTicks();
        while (getTaskHandle()) {
            kernel::delayTicks(pollInterval);
            if (kernel::getTicks() - start_ticks > timeout) {
                return false;
            }
        }

        return true;
    }

    TaskHandle_t getTaskHandle() const { return taskHandle; }

    int32_t getReturnCode() const {
        assert(getState() == State::Stopped);
        return callbackResult;
    }

    uint32_t getStackSpace() const {
        if (xPortInIsrContext() == pdTRUE || getTaskHandle() == nullptr) {
            return 0;
        } else {
            return uxTaskGetStackHighWaterMark(taskHandle) * sizeof(StackType_t);
        }
    }

    static Thread* getCurrent() {
        return static_cast<Thread*>(pvTaskGetThreadLocalStoragePointer(nullptr, LOCAL_STORAGE_SELF_POINTER_INDEX));
    }
};

constexpr auto THREAD_PRIORITY_SERVICE = Thread::Priority::High;
constexpr auto THREAD_PRIORITY_RENDER = Thread::Priority::Higher;
constexpr auto THREAD_PRIORITY_ISR = Thread::Priority::Critical;

} // namespace
