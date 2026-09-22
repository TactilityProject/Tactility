#include <tactility/log.h>
#include <tactility/memory.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#else
#include <cstdio>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

constexpr auto* TAG = "memory";

extern "C" {

const struct MemoryPolicy MEMORY_POLICY_DEFAULT = {
    .required = 0,
    .desired = 0,
    .alignment = 0,
};

void memory_log_stats() {
#ifdef ESP_PLATFORM
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    LOG_I(TAG, "Heap: %zu / %zu available", heap_free, heap_total);
    size_t ext_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ext_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    LOG_I(TAG, "External: %zu / %zu available", ext_free, ext_total);
#else
    auto heap_total = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    auto heap_free = sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE);
    LOG_I(TAG, "Heap: %zu / %zu available", heap_free, heap_total);
#endif
}

}
