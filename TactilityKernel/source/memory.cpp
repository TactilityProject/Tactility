#include <tactility/log.h>
#include <tactility/memory.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#else
#include <cstdint>
#include <cstdio>
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
    const long phys_pages = sysconf(_SC_PHYS_PAGES);
    const long avphys_pages = sysconf(_SC_AVPHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (phys_pages < 0 || avphys_pages < 0 || page_size < 0) {
        LOG_W(TAG, "Heap: sysconf() unavailable");
    } else {
        const uint64_t heap_total = static_cast<uint64_t>(phys_pages) * static_cast<uint64_t>(page_size);
        const uint64_t heap_free = static_cast<uint64_t>(avphys_pages) * static_cast<uint64_t>(page_size);
        LOG_I(TAG, "Heap: %llu / %llu available", static_cast<unsigned long long>(heap_free), static_cast<unsigned long long>(heap_total));
    }
#endif
}

}
