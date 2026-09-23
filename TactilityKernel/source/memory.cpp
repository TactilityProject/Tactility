#include <tactility/log.h>
#include <tactility/memory.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
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

size_t memory_heap_total() {
#ifdef ESP_PLATFORM
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
#elif defined(__APPLE__)
    uint64_t mem_size = 0;
    size_t mem_size_len = sizeof(mem_size);
    if (sysctlbyname("hw.memsize", &mem_size, &mem_size_len, nullptr, 0) != 0) {
        return 0;
    }
    return static_cast<size_t>(mem_size);
#else
    const long phys_pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (phys_pages < 0 || page_size < 0) {
        return 0;
    }
    return static_cast<size_t>(static_cast<uint64_t>(phys_pages) * static_cast<uint64_t>(page_size));
#endif
}

size_t memory_heap_free() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#elif defined(__APPLE__)
    // No sysconf(_SC_AVPHYS_PAGES) on macOS: free space comes from the Mach host VM statistics.
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    vm_statistics64_data_t vm_stats {};
    mach_msg_type_number_t vm_stats_count = HOST_VM_INFO64_COUNT;
    kern_return_t result = host_page_size(host, &page_size);
    if (result == KERN_SUCCESS) {
        result = host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm_stats), &vm_stats_count);
    }
    mach_port_deallocate(mach_task_self(), host);
    if (result != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<size_t>(static_cast<uint64_t>(vm_stats.free_count + vm_stats.inactive_count) * page_size);
#else
    const long avphys_pages = sysconf(_SC_AVPHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (avphys_pages < 0 || page_size < 0) {
        return 0;
    }
    return static_cast<size_t>(static_cast<uint64_t>(avphys_pages) * static_cast<uint64_t>(page_size));
#endif
}

void memory_log_stats() {
#ifdef ESP_PLATFORM
    LOG_I(TAG, "Heap: %zu / %zu available", memory_heap_free(), memory_heap_total());
    size_t ext_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ext_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    LOG_I(TAG, "External: %zu / %zu available", ext_free, ext_total);
#else
    // Not the process's own heap capacity - see memory_heap_total()'s doc comment.
    LOG_I(TAG, "System memory: %zu / %zu available", memory_heap_free(), memory_heap_total());
#endif
}

}
