#include <tactility/freertos/task.h>
#include <tactility/log.h>

static const auto* TAG = LOG_TAG(Kernel);

static void log_memory_info() {
#ifdef ESP_PLATFORM
    LOG_E(TAG, "Default memory caps:");
    LOG_E(TAG, "  Total: %" PRIu64, static_cast<uint64_t>(heap_caps_get_total_size(MALLOC_CAP_DEFAULT)));
    LOG_E(TAG, "  Free: %" PRIu64, static_cast<uint64_t>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT)));
    LOG_E(TAG, "  Min free: %" PRIu64, static_cast<uint64_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT)));
    LOG_E(TAG, "Internal memory caps:");
    LOG_E(TAG, "  Total: %" PRIu64, static_cast<uint64_t>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL)));
    LOG_E(TAG, "  Free: %" PRIu64, static_cast<uint64_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    LOG_E(TAG, "  Min free: %" PRIu64, static_cast<uint64_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
#endif
}

static void log_task_info() {
    const char* name = pcTaskGetName(nullptr);
    const char* safe_name = name ? name : "main";
    LOG_E(TAG, "Task: %s", safe_name);
}

extern "C" {

__attribute__((noreturn)) void __crash(void) {
    log_task_info();
    log_memory_info();
    // TODO: Add breakpoint when debugger is attached.
#ifdef ESP_PLATFORM
    esp_system_abort("System halted. Connect debugger for more info.");
#else
    while (true) { /* Indefinite lock-up */ }
#endif
    __builtin_unreachable();
}

}
