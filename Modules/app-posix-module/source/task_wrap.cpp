// SPDX-License-Identifier: Apache-2.0
#ifndef __APPLE__

// Tasks created by an app's own code are tracked (see app/resources.h).
// FreeRTOS tasks go through the hooks of freertos_task_hooks.h, pthreads through a plain strong definition,
// like the ones in stdio_wrap_elf.cpp. A task the app creates runs as part of that app, within its image.
#include <app_posix/malloc_wrap.h>
#include <app_posix/task_wrap.h>

#include <app/resources.h>

#include <freertos_task_hooks.h>

#include <dlfcn.h>
#include <pthread.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>

namespace {

struct TaskStart {
    AppInstanceId app_instance_id;
    uintptr_t image_start;
    uintptr_t image_end;
};

/** Allocates the parameters for a trampoline, also tracked as an allocation of the app, so it's freed when the task never ran. */
template<typename Start>
Start* start_create() {
    auto* start = static_cast<Start*>(malloc(sizeof(Start)));
    if (start != nullptr) {
        start->task.app_instance_id = app_resources_current_app();
        app_posix_get_current_image(&start->task.image_start, &start->task.image_end);
        app_resources_track_alloc(start);
    }
    return start;
}

template<typename Start>
void start_free(Start* start) {
    app_resources_untrack_alloc(start);
    free(start);
}

/**
 * @param[in] freertos_task false for a plain pthread, which can't take part in the app-libc paths that use FreeRTOS
 * @return false when the task must end without running app code
 */
bool enter_task(const TaskStart& start, bool freertos_task) {
    const bool entered = freertos_task ? app_resources_enter_task(start.app_instance_id) : app_resources_enter_thread(start.app_instance_id);
    if (!entered) {
        return false;
    }
    app_posix_set_current_image(start.image_start, start.image_end);
    return true;
}

// region FreeRTOS tasks

struct FreeRtosTaskStart {
    TaskStart task;
    TaskFunction_t function;
    void* parameter;
};

void freertos_task_trampoline(void* context) {
    auto* start = static_cast<FreeRtosTaskStart*>(context);
    // Only fails when app_resources_release() is about to delete this task
    if (!enter_task(start->task, true)) {
        vTaskSuspend(nullptr);
    }
    const TaskFunction_t function = start->function;
    void* parameter = start->parameter;
    start_free(start);
    function(parameter);
}

void freertos_task_delete(void* handle) {
    freertos_real_vTaskDelete(static_cast<TaskHandle_t>(handle));
}

struct FreeRtosTaskCreate {
    FreeRtosTaskStart* start;
    const char* name;
    configSTACK_DEPTH_TYPE stack_depth;
    UBaseType_t priority;
    TaskHandle_t created;
};

bool freertos_task_create(void* context, void** out_handle) {
    auto* create = static_cast<FreeRtosTaskCreate*>(context);
    if (freertos_real_xTaskCreate(freertos_task_trampoline, create->name, create->stack_depth, create->start, create->priority, &create->created) != pdPASS) {
        return false;
    }
    *out_handle = create->created;
    return true;
}

BaseType_t create_hook(const void* caller, TaskFunction_t function, const char* name, configSTACK_DEPTH_TYPE stack_depth, void* parameter, UBaseType_t priority, TaskHandle_t* out_handle) {
    if (!app_posix_is_app_caller(caller) || app_resources_current_app() == 0) {
        return freertos_real_xTaskCreate(function, name, stack_depth, parameter, priority, out_handle);
    }
    auto* start = start_create<FreeRtosTaskStart>();
    if (start == nullptr) {
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    start->function = function;
    start->parameter = parameter;
    FreeRtosTaskCreate create { start, name, stack_depth, priority, nullptr };
    if (!app_resources_create_task(freertos_task_create, &create, freertos_task_delete, nullptr)) {
        start_free(start);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    if (out_handle != nullptr) {
        *out_handle = create.created;
    }
    return pdPASS;
}

void delete_hook(const void* caller, TaskHandle_t handle) {
    if (app_posix_is_app_caller(caller)) {
        app_resources_untrack_task(handle != nullptr ? handle : xTaskGetCurrentTaskHandle());
    }
    freertos_real_vTaskDelete(handle);
}

// endregion

// region pthreads

struct ThreadStart {
    TaskStart task;
    void* (*function)(void*);
    void* argument;
};

void* thread_key(pthread_t thread) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(thread));
}

int real_pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*function)(void*), void* argument) {
    static auto real = reinterpret_cast<int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*)>(dlsym(RTLD_NEXT, "pthread_create"));
    return real(thread, attributes, function, argument);
}

void* thread_trampoline(void* context) {
    auto* start = static_cast<ThreadStart*>(context);
    if (!enter_task(start->task, false)) {
        return nullptr;
    }
    void* (*function)(void*) = start->function;
    void* argument = start->argument;
    start_free(start);
    void* result = function(argument);
    app_resources_untrack_task(thread_key(pthread_self()));
    return result;
}

constexpr long THREAD_JOIN_TIMEOUT_MS = 500;

pthread_t thread_of(void* handle) {
    return static_cast<pthread_t>(reinterpret_cast<uintptr_t>(handle));
}

// Only takes effect at the thread's next cancellation point
void thread_delete(void* handle) {
    pthread_cancel(thread_of(handle));
}

// A detached thread can't be joined, and might still be running app code when the app is unloaded
void thread_join(void* handle) {
    timespec deadline {};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += THREAD_JOIN_TIMEOUT_MS * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    pthread_timedjoin_np(thread_of(handle), nullptr, &deadline);
}

struct ThreadCreate {
    pthread_t* thread;
    const pthread_attr_t* attributes;
    ThreadStart* start;
};

bool thread_create(void* context, void** out_handle) {
    const auto* create = static_cast<const ThreadCreate*>(context);
    if (real_pthread_create(create->thread, create->attributes, thread_trampoline, create->start) != 0) {
        return false;
    }
    *out_handle = thread_key(*create->thread);
    return true;
}

// endregion

} // namespace

// Never uninstalled: the hooks pass every call through unless an app tracks its resources
void app_posix_install_task_hooks() {
    freertos_set_task_hooks(create_hook, delete_hook);
}

extern "C" {

int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*function)(void*), void* argument) {
    if (!app_posix_is_app_caller(__builtin_return_address(0)) || app_resources_current_app() == 0) {
        return real_pthread_create(thread, attributes, function, argument);
    }
    auto* start = start_create<ThreadStart>();
    if (start == nullptr) {
        return ENOMEM;
    }
    start->function = function;
    start->argument = argument;
    ThreadCreate create { thread, attributes, start };
    if (!app_resources_create_task(thread_create, &create, thread_delete, thread_join)) {
        start_free(start);
        return EAGAIN;
    }
    return 0;
}

void pthread_exit(void* result) {
    app_resources_untrack_task(thread_key(pthread_self()));
    static auto real = reinterpret_cast<void (*)(void*)>(dlsym(RTLD_NEXT, "pthread_exit"));
    real(result);
    __builtin_unreachable();
}

} // extern "C"

#endif
