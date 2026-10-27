// SPDX-License-Identifier: Apache-2.0
#include <tactility/log_queue.h>
#include <tactility/log.h>

#include <tactility/freertos/queue.h>
#include <tactility/freertos/task.h>
#include <tactility/memory.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#if defined(ESP_PLATFORM)
#include <esp_log.h>
#endif

namespace {

constexpr size_t LOG_QUEUE_DEPTH = 8;
constexpr size_t DRAIN_TASK_STACK_DEPTH = 4096 / sizeof(StackType_t);

struct LogQueueMessage {
    uint16_t length;
    char text[LOG_QUEUE_MESSAGE_MAX_LENGTH];
};

std::atomic<QueueHandle_t> g_queue { nullptr };

#if !defined(ESP_PLATFORM)
thread_local bool g_direct_write = false;
#endif

// The one and only place a real console write happens for log output - deliberately never
// through app_io_write()'s fd-table redirection (Modules/app-module/source/io.cpp). That
// redirection only ever triggers for a task app_task_main() (Modules/app-module/source/
// scheduler.cpp) set up as a running app instance; this drain task is never that, so
// app_scheduler_current_app_id() always reads 0 on it and app_io_write()'s lookup is skipped
// unconditionally - a plain write() already reaches the real syscall, on every platform, with no
// wrap-bypassing needed here.
void write_real(const char* data, size_t length) {
    if (length == 0) {
        return;
    }
#if defined(ESP_PLATFORM)
    constexpr int fd = 1; // ESP-IDF's default console fd
#else
    constexpr int fd = 2; // matches log_generic()'s stderr choice
#endif
    ::write(fd, data, length);
}

void drain_task_main(void* context) {
    // The queue handle is already valid by the time this task starts (it's created before the
    // task is), but reading it back from g_queue here would race: g_queue is only published
    // (log_queue_init()'s g_queue.store()) AFTER task creation returns, so this task can start
    // running before that store happens. Take it directly as the task's own argument instead.
    auto queue = static_cast<QueueHandle_t>(context);
    LogQueueMessage message;
    while (true) {
        if (xQueueReceive(queue, &message, portMAX_DELAY) == pdTRUE) {
            write_real(message.text, message.length);
        }
    }
}

#if defined(ESP_PLATFORM)
// ESP-IDF's own log macros pre-format the entire line (color, level letter, timestamp, tag,
// message, color reset, newline - see esp_log_write()/LOG_FORMAT() in esp_log.h) before handing
// it to the installed vprintf_like_t as fmt+args, so this hook only needs one vsnprintf - no
// prefix building of its own (unlike log_generic() in log.cpp, which builds its own prefix).
// Replaces (does not chain through) the previously-installed vprintf: the whole point is
// removing the direct-to-stdout path, not adding a second consumer of it.
int log_queue_vprintf_hook(const char* format, va_list args) {
    char buffer[LOG_QUEUE_MESSAGE_MAX_LENGTH];
    int written = vsnprintf(buffer, sizeof(buffer), format, args);
    if (written > 0) {
        size_t length = static_cast<size_t>(written) < sizeof(buffer) ? static_cast<size_t>(written) : sizeof(buffer) - 1;
        log_queue_write(buffer, length);
    }
    return written;
}
#endif

} // namespace

extern "C" {

void log_queue_init(void) {
    if (g_queue.load(std::memory_order_relaxed) != nullptr) {
        return; // already initialized
    }

#if defined(ESP_PLATFORM)
    // Message storage: prefer PSRAM/external memory, fall back to internal automatically if
    // unavailable (MemoryPolicy's documented semantics) - this is the bulk of the queue's memory
    // (24 * 256 bytes), so it's the part worth offloading to PSRAM when there is any.
    MemoryPolicy storage_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    auto* queue_storage = static_cast<uint8_t*>(memory_alloc_with_policy(LOG_QUEUE_DEPTH * sizeof(LogQueueMessage), &storage_policy));
    if (queue_storage == nullptr) {
        return;
    }

    // The queue's own control block and the drain task's TCB are both small, fixed-size kernel
    // objects (not the bulk data) - keep them in internal RAM like scheduler.cpp's
    // APP_TASK_TCB_POLICY already does for app instance tasks.
    MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };

    auto* queue_struct = static_cast<StaticQueue_t*>(memory_alloc_with_policy(sizeof(StaticQueue_t), &internal_policy));
    if (queue_struct == nullptr) {
        memory_free(queue_storage);
        return;
    }

    QueueHandle_t queue = xQueueCreateStatic(LOG_QUEUE_DEPTH, sizeof(LogQueueMessage), queue_storage, queue_struct);
    if (queue == nullptr) {
        memory_free(queue_struct);
        memory_free(queue_storage);
        return;
    }

    // Stack: same PSRAM-preferred/internal-fallback policy as the message storage above.
    MemoryPolicy stack_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    auto* stack_buffer = static_cast<StackType_t*>(memory_alloc_with_policy(DRAIN_TASK_STACK_DEPTH * sizeof(StackType_t), &stack_policy));
    if (stack_buffer == nullptr) {
        vQueueDelete(queue);
        memory_free(queue_struct);
        memory_free(queue_storage);
        return;
    }

    auto* task_tcb = static_cast<StaticTask_t*>(memory_alloc_with_policy(sizeof(StaticTask_t), &internal_policy));
    if (task_tcb == nullptr) {
        memory_free(stack_buffer);
        vQueueDelete(queue);
        memory_free(queue_struct);
        memory_free(queue_storage);
        return;
    }

    TaskHandle_t task_handle = xTaskCreateStatic(drain_task_main, "log_drain", DRAIN_TASK_STACK_DEPTH, queue, tskIDLE_PRIORITY + 1, stack_buffer, task_tcb);
    if (task_handle == nullptr) {
        memory_free(task_tcb);
        memory_free(stack_buffer);
        vQueueDelete(queue);
        memory_free(queue_struct);
        memory_free(queue_storage);
        return;
    }
#else
    QueueHandle_t queue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(LogQueueMessage));
    if (queue == nullptr) {
        return;
    }

    TaskHandle_t task_handle = nullptr;
    if (xTaskCreate(drain_task_main, "log_drain", DRAIN_TASK_STACK_DEPTH, queue, tskIDLE_PRIORITY + 1, &task_handle) != pdPASS) {
        vQueueDelete(queue);
        return;
    }
#endif

    g_queue.store(queue, std::memory_order_release); // published last

#if defined(ESP_PLATFORM)
    esp_log_set_vprintf(log_queue_vprintf_hook);
#endif
}

#if !defined(ESP_PLATFORM)
void log_set_direct_write_for_current_thread(void) {
    g_direct_write = true;
}
#endif

void log_queue_write(const char* data, size_t length) {
    if (data == nullptr || length == 0) {
        return;
    }

#if !defined(ESP_PLATFORM)
    if (g_direct_write) {
        write_real(data, length);
        return;
    }
#endif

    QueueHandle_t queue = g_queue.load(std::memory_order_acquire);
    if (queue == nullptr) {
        write_real(data, length); // pre-init fallback
        return;
    }

    LogQueueMessage message;
    size_t copy_length = length < sizeof(message.text) ? length : sizeof(message.text) - 1;
    memcpy(message.text, data, copy_length);
    message.length = static_cast<uint16_t>(copy_length);

    xQueueSend(queue, &message, portMAX_DELAY);
}

} // extern "C"
