// SPDX-License-Identifier: Apache-2.0

// Symbols resolved for ELF apps ahead of every other module's (see app_esp32_symbol_resolver()).
// Firmware code calling the same functions never gets here.
// Pending signals are delivered here, at the entry and again when a call was interrupted by one,
// since the app holds no lock of the system itself there.
// Allocations are counted per app instance (see app/memory.h).
// Memory, files and tasks are tracked for apps that request cleanup (see app/resources.h).
#include <app/libc.h>
#include <app/memory.h>
#include <app/resources.h>
#include <app/signal.h>

#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include <tactility/module.h>

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/poll.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>

namespace {

ssize_t app_read(int fd, void* buffer, size_t size) {
    app_signal_deliver_pending();
    const ssize_t result = read(fd, buffer, size);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

ssize_t app_write(int fd, const void* buffer, size_t size) {
    app_signal_deliver_pending();
    const ssize_t result = write(fd, buffer, size);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

int app_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    app_signal_deliver_pending();
    const int result = poll(fds, nfds, timeout);
    if (result < 0 && errno == EINTR) {
        app_signal_deliver_pending();
    }
    return result;
}

int app_usleep(useconds_t usec) {
    app_signal_deliver_pending();
    int result;
    if (!app_libc_try_usleep(usec, &result)) {
        return usleep(usec);
    }
    app_signal_deliver_pending();
    return result;
}

unsigned int app_sleep(unsigned int seconds) {
    app_signal_deliver_pending();
    unsigned int result;
    if (!app_libc_try_sleep(seconds, &result)) {
        return sleep(seconds);
    }
    app_signal_deliver_pending();
    return result;
}

int app_kill(pid_t pid, int sig) {
    const int result = kill(pid, sig);
    // A signal sent to the app itself is delivered before kill() returns
    app_signal_deliver_pending();
    return result;
}

pid_t app_getpid() {
    int result;
    return app_libc_try_getpid(&result) ? result : 0;
}

pid_t app_getppid() {
    int result;
    return app_libc_try_getppid(&result) ? result : 0;
}

void* record_alloc(void* ptr) {
    if (ptr != nullptr) {
        app_memory_record_alloc(heap_caps_get_allocated_size(ptr));
        app_resources_track_alloc(ptr);
    }
    return ptr;
}

void record_free(void* ptr) {
    if (ptr != nullptr) {
        app_memory_record_free(heap_caps_get_allocated_size(ptr));
        app_resources_untrack_alloc(ptr);
    }
}

void* app_malloc(size_t size) {
    return record_alloc(malloc(size));
}

void* app_calloc(size_t count, size_t size) {
    return record_alloc(calloc(count, size));
}

void* app_realloc(void* ptr, size_t size) {
    const size_t old_size = (ptr != nullptr) ? heap_caps_get_allocated_size(ptr) : 0;
    // Untracked before ptr may be freed
    app_resources_untrack_alloc(ptr);
    void* result = realloc(ptr, size);
    // A failed realloc() leaves the old block allocated
    if (result != nullptr || size == 0) {
        if (ptr != nullptr) {
            app_memory_record_free(old_size);
        }
        record_alloc(result);
    } else if (ptr != nullptr) {
        app_resources_track_alloc(ptr);
    }
    return result;
}

void app_free(void* ptr) {
    record_free(ptr);
    free(ptr);
}

char* app_strdup(const char* s) {
    return static_cast<char*>(record_alloc(strdup(s)));
}

char* app_strndup(const char* s, size_t n) {
    return static_cast<char*>(record_alloc(strndup(s, n)));
}

void* app_operator_new(size_t size) {
    return record_alloc(::operator new(size));
}

void* app_operator_new_array(size_t size) {
    return record_alloc(::operator new[](size));
}

void app_operator_delete(void* ptr) {
    record_free(ptr);
    ::operator delete(ptr);
}

void app_operator_delete_array(void* ptr) {
    record_free(ptr);
    ::operator delete[](ptr);
}

void app_operator_delete_sized(void* ptr, size_t) {
    app_operator_delete(ptr);
}

void app_operator_delete_array_sized(void* ptr, size_t) {
    app_operator_delete_array(ptr);
}

// region Files

int app_open(const char* path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    const mode_t mode = (flags & O_CREAT) ? static_cast<mode_t>(va_arg(args, int)) : 0;
    va_end(args);
    const int fd = open(path, flags, mode);
    if (fd >= 0) {
        app_resources_track_fd(fd);
    }
    return fd;
}

int app_close(int fd) {
    app_resources_untrack_fd(fd);
    return close(fd);
}

FILE* app_fopen(const char* path, const char* mode) {
    FILE* file = fopen(path, mode);
    if (file != nullptr) {
        app_resources_track_file(file);
    }
    return file;
}

FILE* app_fdopen(int fd, const char* mode) {
    FILE* file = fdopen(fd, mode);
    if (file != nullptr) {
        // Closed through the FILE from now on
        app_resources_untrack_fd(fd);
        app_resources_track_file(file);
    }
    return file;
}

int app_fclose(FILE* file) {
    app_resources_untrack_file(file);
    return fclose(file);
}

DIR* app_opendir(const char* path) {
    DIR* dir = opendir(path);
    if (dir != nullptr) {
        app_resources_track_dir(dir);
    }
    return dir;
}

int app_closedir(DIR* dir) {
    app_resources_untrack_dir(dir);
    return closedir(dir);
}

// endregion

// region FreeRTOS tasks

struct TaskStart {
    TaskFunction_t function;
    void* parameter;
    AppInstanceId app_instance_id;
};

// Also tracked as an allocation of the app, so it's freed when the task is deleted before it ever ran
TaskStart* task_start_create(TaskFunction_t function, void* parameter) {
    auto* start = static_cast<TaskStart*>(malloc(sizeof(TaskStart)));
    if (start != nullptr) {
        *start = { function, parameter, app_resources_current_app() };
        app_resources_track_alloc(start);
    }
    return start;
}

void task_trampoline(void* context) {
    auto* start = static_cast<TaskStart*>(context);
    // Only fails when app_resources_release() is about to delete this task
    if (!app_resources_enter_task(start->app_instance_id)) {
        vTaskSuspend(nullptr);
    }
    const TaskStart copy = *start;
    app_resources_untrack_alloc(start);
    free(start);
    copy.function(copy.parameter);
}

void delete_task(void* handle) {
    vTaskDelete(static_cast<TaskHandle_t>(handle));
}

void delete_task_with_caps(void* handle) {
    vTaskDeleteWithCaps(static_cast<TaskHandle_t>(handle));
}

struct TaskCreate {
    TaskStart* start;
    const char* name;
    uint32_t stack_depth;
    UBaseType_t priority;
    BaseType_t core;
    // xTaskCreateStaticPinnedToCore() only
    StackType_t* stack_buffer;
    StaticTask_t* task_buffer;
    // xTaskCreatePinnedToCoreWithCaps() only
    bool with_caps;
    UBaseType_t memory_caps;
    TaskHandle_t created;
};

bool create_task(void* context, void** out_handle) {
    auto* create = static_cast<TaskCreate*>(context);
    TaskHandle_t handle = nullptr;
    if (create->stack_buffer != nullptr) {
        handle = xTaskCreateStaticPinnedToCore(task_trampoline, create->name, create->stack_depth, create->start, create->priority, create->stack_buffer, create->task_buffer, create->core);
    } else if (create->with_caps) {
        if (xTaskCreatePinnedToCoreWithCaps(task_trampoline, create->name, create->stack_depth, create->start, create->priority, &handle, create->core, create->memory_caps) != pdPASS) {
            handle = nullptr;
        }
    } else if (xTaskCreatePinnedToCore(task_trampoline, create->name, create->stack_depth, create->start, create->priority, &handle, create->core) != pdPASS) {
        handle = nullptr;
    }
    create->created = handle;
    *out_handle = handle;
    return handle != nullptr;
}

/** @return the created task, or nullptr */
TaskHandle_t create_tracked_task(TaskFunction_t function, void* parameter, TaskCreate create) {
    create.start = task_start_create(function, parameter);
    if (create.start == nullptr) {
        return nullptr;
    }
    if (!app_resources_create_task(create_task, &create, create.with_caps ? delete_task_with_caps : delete_task, nullptr)) {
        app_resources_untrack_alloc(create.start);
        free(create.start);
        return nullptr;
    }
    return create.created;
}

BaseType_t app_xTaskCreatePinnedToCore(TaskFunction_t function, const char* name, uint32_t stack_depth, void* parameter, UBaseType_t priority, TaskHandle_t* out_handle, BaseType_t core) {
    if (app_resources_current_app() == 0) {
        return xTaskCreatePinnedToCore(function, name, stack_depth, parameter, priority, out_handle, core);
    }
    TaskHandle_t handle = create_tracked_task(function, parameter, { nullptr, name, stack_depth, priority, core, nullptr, nullptr, false, 0, nullptr });
    if (out_handle != nullptr) {
        *out_handle = handle;
    }
    return handle != nullptr ? pdPASS : errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
}

BaseType_t app_xTaskCreate(TaskFunction_t function, const char* name, configSTACK_DEPTH_TYPE stack_depth, void* parameter, UBaseType_t priority, TaskHandle_t* out_handle) {
    return app_xTaskCreatePinnedToCore(function, name, stack_depth, parameter, priority, out_handle, tskNO_AFFINITY);
}

TaskHandle_t app_xTaskCreateStaticPinnedToCore(TaskFunction_t function, const char* name, uint32_t stack_depth, void* parameter, UBaseType_t priority, StackType_t* stack_buffer, StaticTask_t* task_buffer, BaseType_t core) {
    if (app_resources_current_app() == 0) {
        return xTaskCreateStaticPinnedToCore(function, name, stack_depth, parameter, priority, stack_buffer, task_buffer, core);
    }
    return create_tracked_task(function, parameter, { nullptr, name, stack_depth, priority, core, stack_buffer, task_buffer, false, 0, nullptr });
}

TaskHandle_t app_xTaskCreateStatic(TaskFunction_t function, const char* name, uint32_t stack_depth, void* parameter, UBaseType_t priority, StackType_t* stack_buffer, StaticTask_t* task_buffer) {
    return app_xTaskCreateStaticPinnedToCore(function, name, stack_depth, parameter, priority, stack_buffer, task_buffer, tskNO_AFFINITY);
}

BaseType_t app_xTaskCreatePinnedToCoreWithCaps(TaskFunction_t function, const char* name, configSTACK_DEPTH_TYPE stack_depth, void* parameter, UBaseType_t priority, TaskHandle_t* out_handle, BaseType_t core, UBaseType_t memory_caps) {
    if (app_resources_current_app() == 0) {
        return xTaskCreatePinnedToCoreWithCaps(function, name, stack_depth, parameter, priority, out_handle, core, memory_caps);
    }
    TaskHandle_t handle = create_tracked_task(function, parameter, { nullptr, name, stack_depth, priority, core, nullptr, nullptr, true, memory_caps, nullptr });
    if (out_handle != nullptr) {
        *out_handle = handle;
    }
    return handle != nullptr ? pdPASS : errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
}

BaseType_t app_xTaskCreateWithCaps(TaskFunction_t function, const char* name, configSTACK_DEPTH_TYPE stack_depth, void* parameter, UBaseType_t priority, TaskHandle_t* out_handle, UBaseType_t memory_caps) {
    return app_xTaskCreatePinnedToCoreWithCaps(function, name, stack_depth, parameter, priority, out_handle, tskNO_AFFINITY, memory_caps);
}

void app_vTaskDelete(TaskHandle_t handle) {
    app_resources_untrack_task(handle != nullptr ? handle : xTaskGetCurrentTaskHandle());
    vTaskDelete(handle);
}

void app_vTaskDeleteWithCaps(TaskHandle_t handle) {
    app_resources_untrack_task(handle != nullptr ? handle : xTaskGetCurrentTaskHandle());
    vTaskDeleteWithCaps(handle);
}

// endregion

// region pthreads

struct ThreadStart {
    void* (*function)(void*);
    void* argument;
    AppInstanceId app_instance_id;
};

// A pthread is tracked by its pthread_t, but deleted through the FreeRTOS task that runs it
std::mutex thread_tasks_mutex;
std::unordered_map<pthread_t, TaskHandle_t> thread_tasks;

void* thread_key(pthread_t thread) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(thread));
}

void thread_ended() {
    const pthread_t self = pthread_self();
    app_resources_untrack_task(thread_key(self));
    std::lock_guard lock(thread_tasks_mutex);
    thread_tasks.erase(self);
}

void* thread_trampoline(void* context) {
    auto* start = static_cast<ThreadStart*>(context);
    if (!app_resources_enter_task(start->app_instance_id)) {
        return nullptr;
    }
    const ThreadStart copy = *start;
    app_resources_untrack_alloc(start);
    free(start);
    {
        std::lock_guard lock(thread_tasks_mutex);
        thread_tasks[pthread_self()] = xTaskGetCurrentTaskHandle();
    }
    void* result = copy.function(copy.argument);
    thread_ended();
    return result;
}

// ESP-IDF's own bookkeeping of the thread leaks
void delete_thread(void* handle) {
    std::lock_guard lock(thread_tasks_mutex);
    auto iterator = thread_tasks.find(static_cast<pthread_t>(reinterpret_cast<uintptr_t>(handle)));
    if (iterator != thread_tasks.end()) {
        vTaskDelete(iterator->second);
        thread_tasks.erase(iterator);
    }
}

struct ThreadCreate {
    pthread_t* thread;
    const pthread_attr_t* attributes;
    ThreadStart* start;
};

bool create_thread(void* context, void** out_handle) {
    const auto* create = static_cast<const ThreadCreate*>(context);
    if (pthread_create(create->thread, create->attributes, thread_trampoline, create->start) != 0) {
        return false;
    }
    *out_handle = thread_key(*create->thread);
    return true;
}

int app_pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*function)(void*), void* argument) {
    if (app_resources_current_app() == 0) {
        return pthread_create(thread, attributes, function, argument);
    }
    auto* start = static_cast<ThreadStart*>(malloc(sizeof(ThreadStart)));
    if (start == nullptr) {
        return ENOMEM;
    }
    *start = { function, argument, app_resources_current_app() };
    app_resources_track_alloc(start);
    ThreadCreate create { thread, attributes, start };
    if (!app_resources_create_task(create_thread, &create, delete_thread, nullptr)) {
        app_resources_untrack_alloc(start);
        free(start);
        return EAGAIN;
    }
    return 0;
}

void app_pthread_exit(void* result) {
    thread_ended();
    pthread_exit(result);
}

// endregion

} // namespace

extern "C" {

extern const ModuleSymbol app_esp32_symbols[] = {
    { "read", reinterpret_cast<void*>(app_read) },
    { "write", reinterpret_cast<void*>(app_write) },
    { "poll", reinterpret_cast<void*>(app_poll) },
    { "usleep", reinterpret_cast<void*>(app_usleep) },
    { "sleep", reinterpret_cast<void*>(app_sleep) },
    { "kill", reinterpret_cast<void*>(app_kill) },
    { "getpid", reinterpret_cast<void*>(app_getpid) },
    { "getppid", reinterpret_cast<void*>(app_getppid) },
    { "malloc", reinterpret_cast<void*>(app_malloc) },
    { "calloc", reinterpret_cast<void*>(app_calloc) },
    { "realloc", reinterpret_cast<void*>(app_realloc) },
    { "free", reinterpret_cast<void*>(app_free) },
    { "strdup", reinterpret_cast<void*>(app_strdup) },
    { "strndup", reinterpret_cast<void*>(app_strndup) },
    { "_Znwj", reinterpret_cast<void*>(app_operator_new) }, // operator new(unsigned int)
    { "_Znaj", reinterpret_cast<void*>(app_operator_new_array) }, // operator new[](unsigned int)
    { "_ZdlPv", reinterpret_cast<void*>(app_operator_delete) }, // operator delete(void*)
    { "_ZdaPv", reinterpret_cast<void*>(app_operator_delete_array) }, // operator delete[](void*)
    { "_ZdlPvj", reinterpret_cast<void*>(app_operator_delete_sized) }, // operator delete(void*, unsigned int)
    { "_ZdaPvj", reinterpret_cast<void*>(app_operator_delete_array_sized) }, // operator delete[](void*, unsigned int)
    { "open", reinterpret_cast<void*>(app_open) },
    { "close", reinterpret_cast<void*>(app_close) },
    { "fopen", reinterpret_cast<void*>(app_fopen) },
    { "fdopen", reinterpret_cast<void*>(app_fdopen) },
    { "fclose", reinterpret_cast<void*>(app_fclose) },
    { "opendir", reinterpret_cast<void*>(app_opendir) },
    { "closedir", reinterpret_cast<void*>(app_closedir) },
    { "xTaskCreate", reinterpret_cast<void*>(app_xTaskCreate) },
    { "xTaskCreatePinnedToCore", reinterpret_cast<void*>(app_xTaskCreatePinnedToCore) },
    { "xTaskCreateStatic", reinterpret_cast<void*>(app_xTaskCreateStatic) },
    { "xTaskCreateStaticPinnedToCore", reinterpret_cast<void*>(app_xTaskCreateStaticPinnedToCore) },
    { "xTaskCreateWithCaps", reinterpret_cast<void*>(app_xTaskCreateWithCaps) },
    { "xTaskCreatePinnedToCoreWithCaps", reinterpret_cast<void*>(app_xTaskCreatePinnedToCoreWithCaps) },
    { "vTaskDelete", reinterpret_cast<void*>(app_vTaskDelete) },
    { "vTaskDeleteWithCaps", reinterpret_cast<void*>(app_vTaskDeleteWithCaps) },
    { "pthread_create", reinterpret_cast<void*>(app_pthread_create) },
    { "pthread_exit", reinterpret_cast<void*>(app_pthread_exit) },
    MODULE_SYMBOL_TERMINATOR
};

}
