// SPDX-License-Identifier: Apache-2.0
#include <tactility/concurrent/thread.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <cstdlib>
#include <cstring>
#include <string>

static const size_t LOCAL_STORAGE_SELF_POINTER_INDEX = 0;
static const char* TAG = LOG_TAG(Thread);

struct Thread {
    TaskHandle_t taskHandle = nullptr;
    ThreadState state = THREAD_STATE_STOPPED;
    thread_main_fn_t mainFunction = nullptr;
    void* mainFunctionContext = nullptr;
    int32_t callbackResult = 0;
    thread_state_callback_t stateCallback = nullptr;
    void* stateCallbackContext = nullptr;
    std::string name = "unnamed";
    enum ThreadPriority priority = THREAD_PRIORITY_NORMAL;
    struct Mutex mutex = { 0 };
    configSTACK_DEPTH_TYPE stackSize = 4096;
    portBASE_TYPE affinity = -1;

    Thread() {
        mutex_construct(&mutex);
    }

    ~Thread() {
        mutex_destruct(&mutex);
    }

    void lock() { mutex_lock(&mutex); }

    void unlock() { mutex_unlock(&mutex); }
};

static void thread_set_state_internal(Thread* thread, ThreadState newState) {
    thread->lock();
    thread->state = newState;
    auto cb = thread->stateCallback;
    auto cb_ctx = thread->stateCallbackContext;
    thread->unlock();
    if (cb) {
        cb(newState, cb_ctx);
    }
}

static void thread_main_body(void* context) {
    check(context != nullptr);
    auto* thread = static_cast<Thread*>(context);

    // Save Thread instance pointer to task local storage
    check(pvTaskGetThreadLocalStoragePointer(nullptr, LOCAL_STORAGE_SELF_POINTER_INDEX) == nullptr);
    vTaskSetThreadLocalStoragePointer(nullptr, LOCAL_STORAGE_SELF_POINTER_INDEX, thread);

    LOG_I(TAG, "Starting %s", thread->name.c_str()); // No need to lock as we don't allow mutation after thread start
    check(thread->state == THREAD_STATE_STARTING);
    thread_set_state_internal(thread, THREAD_STATE_RUNNING);

    int32_t result = thread->mainFunction(thread->mainFunctionContext);
    thread->lock();
    thread->callbackResult = result;
    thread->unlock();

    check(thread->state == THREAD_STATE_RUNNING);
    thread_set_state_internal(thread, THREAD_STATE_STOPPED);
    LOG_I(TAG, "Stopped %s", thread->name.c_str()); // No need to lock as we don't allow mutation after thread start

    vTaskSetThreadLocalStoragePointer(nullptr, LOCAL_STORAGE_SELF_POINTER_INDEX, nullptr);

    thread->lock();
    thread->taskHandle = nullptr;
    thread->unlock();

    vTaskDelete(nullptr);
}

extern "C" {

Thread* thread_alloc(void) {
    auto* thread = new(std::nothrow) Thread();
    if (thread == nullptr) {
        return nullptr;
    }
    return thread;
}

Thread* thread_alloc_full(
    const char* name,
    configSTACK_DEPTH_TYPE stack_size,
    thread_main_fn_t function,
    void* function_context,
    portBASE_TYPE affinity
) {
    auto* thread = new(std::nothrow) Thread();
    if (thread != nullptr) {
        thread_set_name(thread, name);
        thread_set_stack_size(thread, stack_size);
        thread_set_main_function(thread, function, function_context);
        thread_set_affinity(thread, affinity);
    }
    return thread;
}

void thread_free(Thread* thread) {
    check(thread);
    check(thread->state == THREAD_STATE_STOPPED);
    check(thread->taskHandle == nullptr);
    delete thread;
}

void thread_set_name(Thread* thread, const char* name) {
    thread->lock();
    check(thread->state == THREAD_STATE_STOPPED);
    thread->name = name;
    thread->unlock();
}

void thread_set_stack_size(Thread* thread, size_t stack_size) {
    thread->lock();
    check(thread->state == THREAD_STATE_STOPPED);
    thread->stackSize = stack_size;
    thread->unlock();
}

void thread_set_affinity(Thread* thread, portBASE_TYPE affinity) {
    thread->lock();
    check(thread->state == THREAD_STATE_STOPPED);
    thread->affinity = affinity;
    thread->unlock();
}

void thread_set_main_function(Thread* thread, thread_main_fn_t function, void* context) {
    thread->lock();
    check(thread->state == THREAD_STATE_STOPPED);
    thread->mainFunction = function;
    thread->mainFunctionContext = context;
    thread->unlock();
}

void thread_set_priority(Thread* thread, enum ThreadPriority priority) {
    thread->lock();
    check(thread->state == THREAD_STATE_STOPPED);
    thread->priority = priority;
    thread->unlock();
}

void thread_set_state_callback(Thread* thread, thread_state_callback_t callback, void* context) {
    thread->lock();
    check(thread->state == THREAD_STATE_STOPPED);
    thread->stateCallback = callback;
    thread->stateCallbackContext = context;
    thread->unlock();
}

ThreadState thread_get_state(Thread* thread) {
    check(xPortInIsrContext() == pdFALSE);
    thread->lock();
    ThreadState state = thread->state;
    thread->unlock();
    return state;
}

error_t thread_start(Thread* thread) {
    thread->lock();
    check(thread->mainFunction != nullptr);
    check(thread->state == THREAD_STATE_STOPPED);
    check(thread->stackSize);
    thread->unlock();

    thread_set_state_internal(thread, THREAD_STATE_STARTING);

    thread->lock();
    uint32_t stack_depth = thread->stackSize / sizeof(StackType_t);
    enum ThreadPriority priority = thread->priority;
    portBASE_TYPE affinity = thread->affinity;
    thread->unlock();

    BaseType_t result;
    if (affinity != -1) {
#ifdef ESP_PLATFORM
        result = xTaskCreatePinnedToCore(
            thread_main_body,
            thread->name.c_str(),
            stack_depth,
            thread,
            (UBaseType_t)priority,
            &thread->taskHandle,
            affinity
        );
#else
        result = xTaskCreate(
            thread_main_body,
            thread->name.c_str(),
            stack_depth,
            thread,
            (UBaseType_t)priority,
            &thread->taskHandle
        );
#endif
    } else {
        result = xTaskCreate(
            thread_main_body,
            thread->name.c_str(),
            stack_depth,
            thread,
            (UBaseType_t)priority,
            &thread->taskHandle
        );
    }

    if (result != pdPASS) {
        thread_set_state_internal(thread, THREAD_STATE_STOPPED);
        thread->lock();
        thread->taskHandle = nullptr;
        thread->unlock();
        return ERROR_UNDEFINED;
    }

    return ERROR_NONE;
}

error_t thread_join(Thread* thread, TickType_t timeout, TickType_t poll_interval) {
    check(thread_get_current() != thread);

    TickType_t start_ticks = get_ticks();
    while (thread_get_task_handle(thread)) {
        delay_ticks(poll_interval);
        if (get_ticks() - start_ticks > timeout) {
            return ERROR_TIMEOUT;
        }
    }

    return ERROR_NONE;
}

TaskHandle_t thread_get_task_handle(Thread* thread) {
    thread->lock();
    auto* handle = thread->taskHandle;
    thread->unlock();
    return handle;
}

int32_t thread_get_return_code(Thread* thread) {
    thread->lock();
    check(thread->state == THREAD_STATE_STOPPED);
    auto result = thread->callbackResult;
    thread->unlock();
    return result;
}

uint32_t thread_get_stack_space(Thread* thread) {
    if (xPortInIsrContext() == pdTRUE) {
        return 0;
    }
    thread->lock();
    check(thread->state == THREAD_STATE_RUNNING);
    auto result = uxTaskGetStackHighWaterMark(thread->taskHandle) * sizeof(StackType_t);
    thread->unlock();
    return result;
}

Thread* thread_get_current(void) {
    check(xPortInIsrContext() == pdFALSE);
    return (Thread*)pvTaskGetThreadLocalStoragePointer(nullptr, LOCAL_STORAGE_SELF_POINTER_INDEX);
}

}
