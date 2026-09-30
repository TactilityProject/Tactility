// SPDX-License-Identifier: Apache-2.0
#include <app/instance.h>
#include <app/loader.h>
#include <app/private/arguments.h>
#include <app/private/event.h>
#include <app/private/fd_table.h>
#include <app/private/ledger.h>
#include <app/private/scheduler.h>
#include <app/private/stream_internal.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <service/instance.h>
#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/delay.h>
#include <tactility/memory.h>
#include <tactility/time.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <new>

constexpr auto* TAG = "app_scheduler";

// The app instance whose task this thread is, so code running on an app's own task can retrieve it via
// app_scheduler_current_app_id() without it being threaded through as a parameter.
#ifdef ESP_PLATFORM
// A FreeRTOS task-local slot rather than thread_local, which faults before the scheduler starts
// (e.g. a write() during early boot). Slot 0 is reserved by ESP-IDF's pthread API.
constexpr size_t APP_INSTANCE_ID_THREAD_SLOT_INDEX = 1;

AppInstanceId get_current_app_id() {
    // No current task before the scheduler starts (e.g. newlib's stdio setup calling fstat()),
    // where pvTaskGetThreadLocalStoragePointer(nullptr, ...) would dereference a null task.
    if (xTaskGetCurrentTaskHandle() == nullptr) {
        return 0;
    }
    return reinterpret_cast<uintptr_t>(pvTaskGetThreadLocalStoragePointer(nullptr, APP_INSTANCE_ID_THREAD_SLOT_INDEX));
}

void set_current_app_id(AppInstanceId app_instance_id) {
    vTaskSetThreadLocalStoragePointer(nullptr, APP_INSTANCE_ID_THREAD_SLOT_INDEX, reinterpret_cast<void*>(static_cast<uintptr_t>(app_instance_id)));
}
#else
// thread_local rather than a FreeRTOS task-local slot: on the simulator, that slot is read from whichever
// task is scheduled, even by a foreign thread (e.g. SDL's), which must never be mistaken for an app.
thread_local AppInstanceId current_app_id = 0;

AppInstanceId get_current_app_id() {
    return current_app_id;
}

void set_current_app_id(AppInstanceId app_instance_id) {
    current_app_id = app_instance_id;
}
#endif

// Matches TactilityKernel's Thread wrapper's THREAD_PRIORITY_NORMAL.
constexpr UBaseType_t APP_TASK_PRIORITY = 4;

// Used when an app's manifest doesn't request a specific stack depth (0). 8192 bytes' worth.
constexpr size_t APP_DEFAULT_STACK_DEPTH = 8192 / sizeof(StackType_t);

// Task control blocks must stay in internal RAM; only the stack itself may live in external memory.
constexpr MemoryPolicy APP_TASK_TCB_POLICY = { MEMORY_CAPABILITY_INTERNAL, 0, 0 };

constexpr auto* APP_REAPER_TASK_NAME = "app_reaper";
constexpr size_t APP_REAPER_STACK_DEPTH = 2048 / sizeof(StackType_t);

namespace {

struct TaskContext {
    const AppLoaderApi* loader;
    void* runtime;
    AppInstanceId app_instance_id;
    /** Nullable, null-terminated. */
    char** argv;
    AppCompletionSignal* completion;
    StackType_t* stackBuffer;
    StaticTask_t* taskTcb;
};

struct ReaperContext {
    TaskHandle_t target;
    StackType_t* stackBuffer;
    StaticTask_t* taskTcb;
};

void reaper_task_main(void* context) {
    auto* ctx = static_cast<ReaperContext*>(context);

    while (eTaskGetState(ctx->target) == eRunning) {
        taskYIELD();
    }
    vTaskDelete(ctx->target);
    memory_free(ctx->stackBuffer);
    memory_free(ctx->taskTcb);
    delete ctx;

    vTaskDelete(nullptr);
}

// Hands off this task's own statically-allocated stack/TCB (which it can never safely free
// itself - a task can't free the stack it's still running on, see UsbHidInput.cpp for the same
// constraint) to a short-lived helper task, then suspends forever. Never returns.
void reap_self(StackType_t* stack_buffer, StaticTask_t* task_tcb) {
    auto* reaper_ctx = new (std::nothrow) ReaperContext { xTaskGetCurrentTaskHandle(), stack_buffer, task_tcb };
    if (reaper_ctx == nullptr || xTaskCreate(reaper_task_main, APP_REAPER_TASK_NAME, APP_REAPER_STACK_DEPTH, reaper_ctx, tskIDLE_PRIORITY, nullptr) != pdPASS) {
        LOG_E(TAG, "Failed to create app reaper task; leaking stack buffer");
        delete reaper_ctx;
    }
    vTaskSuspend(nullptr);
}

void set_state(AppInstanceId app_instance_id, AppInstanceState state) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    if (iterator != ledger.instances.end()) {
        iterator->second.state = state;
    }
    mutex_unlock(&ledger.mutex);
}

// Set while this thread is an app instance's task running AppLoaderApi::run().
thread_local TaskContext* current_task_context = nullptr;

#ifndef ESP_PLATFORM
// On the simulator, a task ending itself unwinds its whole stack (pthread_exit()). After exit(), that
// stack still holds the app's own frames, so its binary may only be unloaded once the unwind has
// passed them: this guard lives in app_task_main()'s frame and unloads when the unwind destroys it.
struct DeferredUnload {
    const AppLoaderApi* loader = nullptr;
    void* runtime = nullptr;

    ~DeferredUnload() {
        if (loader != nullptr) {
            loader->unload(runtime);
            pending_deferred_unloads.fetch_sub(1, std::memory_order_release);
        }
    }

    // A binary that is still loaded would be reused by the next load of the same path, globals included
    static inline std::atomic<int> pending_deferred_unloads { 0 };
};

thread_local DeferredUnload* current_deferred_unload = nullptr;

constexpr TickType_t DEFERRED_UNLOAD_WAIT_TICKS = pdMS_TO_TICKS(1000);

void wait_for_deferred_unloads() {
    const TickType_t start = get_ticks();
    while (DeferredUnload::pending_deferred_unloads.load(std::memory_order_acquire) > 0
        && get_timeout_remaining_ticks(DEFERRED_UNLOAD_WAIT_TICKS, start) > 0) {
        delay_ticks(1);
    }
}
#endif

void set_task(AppInstanceId app_instance_id, TaskHandle_t task) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    if (iterator != ledger.instances.end()) {
        iterator->second.task = task;
        // Streams bound before this instance's task existed (app_start_with_streams())
        // only got producer_task filled in as NULL at subscribe time. Backfill it now.
        AppFdTable& fd_table = iterator->second.fd_table;
        for (auto& slot : fd_table.slots) {
            if (slot.in_use && slot.file.ops == app_stream_ops()) {
                static_cast<AppStream*>(slot.file.object)->producer_task = task;
            }
        }
    }
    mutex_unlock(&ledger.mutex);
}

void set_completion(AppInstanceId app_instance_id, AppCompletionSignal* completion) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    if (iterator != ledger.instances.end()) {
        iterator->second.completion = completion;
    }
    mutex_unlock(&ledger.mutex);
}

// Takes a reference on app_instance_id's completion signal (see AppCompletionSignal), for the
// caller to wait on. @return the signal to wait on, or NULL if the instance has already fully
// finished (its ledger entry - and so its reference to the signal - is already gone) and so
// there's nothing left to wait for, or if the instance is still starting up (start_internal()
// in manager.cpp inserts the ledger entry before app_scheduler_start() has gotten as far as
// set_completion() - `completion` is NULL for that whole window) and so there's nothing to
// take a reference on yet.
AppCompletionSignal* acquire_completion_signal(AppInstanceId app_instance_id) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    AppCompletionSignal* completion = nullptr;
    if (iterator != ledger.instances.end() && iterator->second.completion != nullptr) {
        completion = iterator->second.completion;
        completion->refcount++;
    }
    mutex_unlock(&ledger.mutex);
    return completion;
}

// Releases a reference taken by acquire_completion_signal(), deleting the signal (and its
// semaphore) if this was the last one.
void release_completion_signal(AppCompletionSignal* completion) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    bool should_delete = (--completion->refcount == 0);
    mutex_unlock(&ledger.mutex);
    if (should_delete) {
        vSemaphoreDelete(completion->semaphore);
        delete completion;
    }
}

const char* loader_service_id_for(AppLocationType type) {
    return (type == APP_LOCATION_MEMORY) ? APP_LOADER_MEMORY_SERVICE_ID : APP_LOADER_PATH_SERVICE_ID;
}

const AppLoaderApi* find_loader_api(AppLocationType type) {
    ServiceInstance* instance = service_manager_find_instance(loader_service_id_for(type));
    if (instance == nullptr) {
        return nullptr;
    }
    return static_cast<const AppLoaderApi*>(service_instance_get_data(instance));
}

// If this instance was launched via app_start_for_result(), delivers @a result (its
// own AppMainFn/AppLoaderApi::run() return value) to its parent. No-op for a top-level instance
// (parent_id == 0).
void deliver_result_to_parent_if_any(AppInstanceId app_instance_id, int32_t result) {
    auto& ledger = app_ledger();

    AppInstanceId parent_id;
    AppEvent event { .type = APP_EVENT_RESULT, .timestamp = 0, .result = {} };

    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        return;
    }
    parent_id = iterator->second.parent_id;
    event.result.launch_id = app_instance_id;
    event.result.result = result;
    mutex_unlock(&ledger.mutex);

    if (parent_id != 0) {
        app_event_emit(parent_id, &event);
    }
}

void finish_app_task(TaskContext* ctx, int32_t result, bool exiting);

void app_task_main(void* context) {
    auto* ctx = static_cast<TaskContext*>(context);
#ifndef ESP_PLATFORM
    DeferredUnload deferred_unload;
    current_deferred_unload = &deferred_unload;
#endif

    check(get_current_app_id() == 0);
    set_current_app_id(ctx->app_instance_id);

    // Debug logging so it's invisible by default
    // When logging happens, it can distort the application stdout, which breaks apps that use
    // stdout to output important information, such as the file selection dialog app.
    LOG_I(TAG, "[instance %lu] Task started", ctx->app_instance_id);

    set_state(ctx->app_instance_id, APP_INSTANCE_STATE_ACTIVE);

    current_task_context = ctx;
    int32_t result = ctx->loader->run(ctx->runtime, ctx->app_instance_id, app_arguments_count_null_terminated(ctx->argv), ctx->argv);
    current_task_context = nullptr;

    finish_app_task(ctx, result, false);
}

// Everything after an app's AppLoaderApi::run() returned, or after it called exit(). Ends the calling task.
void finish_app_task(TaskContext* ctx, int32_t result, bool exiting) {
    // The platform might buffer stdout (e.g. esp-idf with newlib)
    // Do a manual flush to ensure data has been written:
    fflush(stdout);

    set_current_app_id(0);

#ifndef ESP_PLATFORM
    if (exiting) {
        current_deferred_unload->loader = ctx->loader;
        current_deferred_unload->runtime = ctx->runtime;
        DeferredUnload::pending_deferred_unloads.fetch_add(1, std::memory_order_release);
    } else {
        ctx->loader->unload(ctx->runtime);
    }
#else
    (void)exiting;
    ctx->loader->unload(ctx->runtime);
#endif

    deliver_result_to_parent_if_any(ctx->app_instance_id, result);

    // The terminal marker for every exit path: an app instance is Stopped exactly when its
    // AppMainFn/AppLoaderApi::run() has returned, whether that return was self-initiated or in
    // response to APP_EVENT_CLOSE.
    set_state(ctx->app_instance_id, APP_INSTANCE_STATE_STOPPED);

    app_arguments_free_null_terminated(ctx->argv);

    AppInstanceId app_instance_id = ctx->app_instance_id;
    AppCompletionSignal* completion = ctx->completion;
    StackType_t* stack_buffer = ctx->stackBuffer;
    StaticTask_t* task_tcb = ctx->taskTcb;
    delete ctx;

    LOG_I(TAG, "[instance %lu] Task finished", app_instance_id);

    // Erase the ledger entry before self-deleting - see "Reap self-terminated app tasks":
    // nothing else is guaranteed to ever call app_scheduler_stop() for this instance (the
    // common case is the app just closing itself), so this can't wait for that to happen.
    // Every non-null fd is closed here too. Stream-backed entries wake/mark closed whoever is
    // on the other end (e.g. a parent reading this instance's stdout).
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto fd_table_iterator = ledger.instances.find(app_instance_id);
    if (fd_table_iterator != ledger.instances.end()) {
        app_fd_table_teardown(&fd_table_iterator->second.fd_table);
    }
    ledger.instances.erase(app_instance_id);
    mutex_unlock(&ledger.mutex);

    // Signal completion as the literal last action before this task ceases to exist, so
    // app_scheduler_stop() can't observe "stopped" one step early - unlike watching the ledger
    // entry disappear, this can only happen once the task is truly done running. A dedicated
    // semaphore rather than this task's default FreeRTOS notification, since app_event.cpp's
    // AppEventSubscription also uses that shared slot - an unrelated event (e.g. a child's
    // APP_EVENT_RESULT) delivered to this same task could otherwise unblock a concurrent
    // app_scheduler_stop() early.
    xSemaphoreGive(completion->semaphore);
    release_completion_signal(completion); // releases app_task_main()'s own reference

    LOG_I(TAG, "[instance %lu] minimum free stack space: %d bytes", app_instance_id, uxTaskGetStackHighWaterMark(nullptr));
#ifdef ESP_PLATFORM
    // Statically-allocated stack/TCB (see app_scheduler_start()) - can't self-delete, see reap_self().
    reap_self(stack_buffer, task_tcb);
#else
    vTaskDelete(nullptr);
#endif
}

} // namespace

extern "C" {

error_t app_scheduler_start(AppInstanceId app_instance_id, const AppStartContext* start_context) {
    const AppLocation location = start_context->location;
    const AppStackConfig stack = start_context->stack;
    const int argc = start_context->argc;

    const AppLoaderApi* loader = find_loader_api(location.type);
    if (loader == nullptr) {
        LOG_E(TAG, "[instance %lu] No app loader is registered (service '%s' not found)", app_instance_id, loader_service_id_for(location.type));
        return ERROR_NOT_FOUND;
    }

    void* runtime = nullptr;
#ifndef ESP_PLATFORM
    wait_for_deferred_unloads();
#endif
    error_t load_result = loader->load(location, &runtime);
    if (load_result != ERROR_NONE) {
        LOG_E(TAG, "[instance %lu] Failed to load app: %s", app_instance_id, error_to_string(load_result));
        return load_result;
    }

    auto* completion = new (std::nothrow) AppCompletionSignal();
    if (completion == nullptr) {
        LOG_E(TAG, "[instance %lu] Failed to allocate app", app_instance_id);
        loader->unload(runtime);
        return ERROR_OUT_OF_MEMORY;
    }

    completion->semaphore = xSemaphoreCreateBinary();
    if (completion->semaphore == nullptr) {
        LOG_E(TAG, "[instance %lu] Failed to allocate app", app_instance_id);
        delete completion;
        loader->unload(runtime);
        return ERROR_OUT_OF_MEMORY;
    }

    if (stack.depth == 0) {
        LOG_W(TAG, "[instance %lu] using default stack depth", app_instance_id);
    }
    size_t effective_stack_depth = stack.depth != 0 ? stack.depth : APP_DEFAULT_STACK_DEPTH;

    char** argv = app_arguments_copy(argc, start_context->argv);
    if (argc > 0 && argv == nullptr) {
        LOG_W(TAG, "[instance %lu] Failed to allocate app", app_instance_id);
        vSemaphoreDelete(completion->semaphore);
        delete completion;
        loader->unload(runtime);
        return ERROR_OUT_OF_MEMORY;
    }

#ifdef ESP_PLATFORM
    // ESP-IDF's FreeRTOS port has configSUPPORT_STATIC_ALLOCATION, POSIX doesn't
    // Try the desired capability first (if any). If it fails or isn't specified, use the fallback/default alloc behaviour (use internal memory).
    StackType_t* stack_buffer = nullptr;
    if (stack.desired_memory_capability != 0) {
        MemoryPolicy requested_policy = { .required = stack.desired_memory_capability, .desired = 0, .alignment = 0 };
        stack_buffer = static_cast<StackType_t*>(memory_alloc_with_policy(effective_stack_depth * sizeof(StackType_t), &requested_policy));
    }
    if (stack_buffer == nullptr) {
        MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
        stack_buffer = static_cast<StackType_t*>(memory_alloc_with_policy(effective_stack_depth * sizeof(StackType_t), &internal_policy));
    }
    if (stack_buffer == nullptr) {
        LOG_E(TAG, "[instance %lu] Failed to allocate app", app_instance_id);
        vSemaphoreDelete(completion->semaphore);
        delete completion;
        loader->unload(runtime);
        app_arguments_free(argc, argv);
        return ERROR_OUT_OF_MEMORY;
    }

    auto* task_tcb = static_cast<StaticTask_t*>(memory_alloc_with_policy(sizeof(StaticTask_t), &APP_TASK_TCB_POLICY));
    if (task_tcb == nullptr) {
        LOG_E(TAG, "[instance %lu] Failed to allocate app", app_instance_id);
        memory_free(stack_buffer);
        vSemaphoreDelete(completion->semaphore);
        delete completion;
        loader->unload(runtime);
        app_arguments_free(argc, argv);
        return ERROR_OUT_OF_MEMORY;
    }
#else
    StackType_t* stack_buffer = nullptr;
    StaticTask_t* task_tcb = nullptr;
#endif

    auto* context = new (std::nothrow) TaskContext {
        .loader = loader,
        .runtime = runtime,
        .app_instance_id = app_instance_id,
        .argv = argv,
        .completion = completion,
        .stackBuffer = stack_buffer,
        .taskTcb = task_tcb,
    };

    if (context == nullptr) {
        LOG_E(TAG, "[instance %lu] Failed to allocate app", app_instance_id);
#ifdef ESP_PLATFORM
        memory_free(task_tcb);
        memory_free(stack_buffer);
#endif
        vSemaphoreDelete(completion->semaphore);
        delete completion;
        loader->unload(runtime);
        app_arguments_free(argc, argv);
        return ERROR_OUT_OF_MEMORY;
    }

    char task_name[16];
    snprintf(task_name, sizeof(task_name), "app_%lu", static_cast<unsigned long>(app_instance_id));

    // Created at idle priority so it can't preempt us before vTaskSuspend() below runs, then suspended immediately -
    // the ledger must record the handle (set_task()) before the task can possibly observe or erase its own entry.
    // (see app_scheduler_stop()'s liveness check and app_task_main()'s exit path)
#ifdef ESP_PLATFORM
    TaskHandle_t task_handle = xTaskCreateStatic(app_task_main, task_name, effective_stack_depth, context, tskIDLE_PRIORITY, stack_buffer, task_tcb);
#else
    TaskHandle_t task_handle = nullptr;
    if (xTaskCreate(app_task_main, task_name, effective_stack_depth, context, tskIDLE_PRIORITY, &task_handle) != pdPASS) {
        task_handle = nullptr;
    }
#endif
    if (task_handle == nullptr) {
        delete context;
#ifdef ESP_PLATFORM
        memory_free(task_tcb);
        memory_free(stack_buffer);
#endif
        vSemaphoreDelete(completion->semaphore);
        delete completion;
        loader->unload(runtime);
        app_arguments_free(argc, argv);
        return ERROR_OUT_OF_MEMORY;
    }
    vTaskSuspend(task_handle);

    set_task(app_instance_id, task_handle);
    set_completion(app_instance_id, completion);
    vTaskPrioritySet(task_handle, APP_TASK_PRIORITY);
    vTaskResume(task_handle);

    memory_log_stats();

    return ERROR_NONE;
}

error_t app_scheduler_stop(AppInstanceId app_instance_id, TickType_t join_timeout) {
    if (app_scheduler_current_app_id() == app_instance_id) {
        LOG_E(TAG, "Can't call app_scheduler_stop() from the owning task");
        return ERROR_NOT_ALLOWED;
    }

    AppCompletionSignal* completion = acquire_completion_signal(app_instance_id);
    if (completion != nullptr) {
        AppEvent event { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
        app_event_emit(app_instance_id, &event);

        // Marked as soon as the app has been told to close, not once its task has actually
        // unwound - so app_manager_get_state()/app_manager_get_topmost_instance_id() reflect the
        // closure immediately, without waiting on whatever teardown the app still has left to do.
        set_state(app_instance_id, APP_INSTANCE_STATE_STOPPING);

        // Blocks until app_task_main() gives this dedicated semaphore as the literal last thing it does before vTaskDelete().
        // Uses aa dedicated semaphore rather than this task's default FreeRTOS notification because app_event.cpp's AppEventSubscription also uses that shared slot.
        // An unrelated event (e.g. a different child's APP_EVENT_RESULT) delivered to this same task could otherwise unblock this early.
        BaseType_t taken = xSemaphoreTake(completion->semaphore, join_timeout);
        release_completion_signal(completion);

        if (taken == pdFALSE) {
            LOG_W(TAG, "App instance %u did not stop in time", app_instance_id);
            return ERROR_TIMEOUT;
        }
    }

    set_state(app_instance_id, APP_INSTANCE_STATE_STOPPED);

    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    ledger.instances.erase(app_instance_id);
    mutex_unlock(&ledger.mutex);

    return ERROR_NONE;
}

AppInstanceId app_scheduler_current_app_id(void) {
    return get_current_app_id();
}

void app_scheduler_exit_current(int32_t status) {
    // Checked first: thread_local can't be read before the scheduler starts on ESP32
    if (get_current_app_id() == 0) {
        return;
    }
    TaskContext* ctx = current_task_context;
    if (ctx == nullptr) {
        return;
    }
    current_task_context = nullptr;
    finish_app_task(ctx, status, true);
}

} // extern "C"
