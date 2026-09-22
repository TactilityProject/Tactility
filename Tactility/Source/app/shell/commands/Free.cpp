#include <Tactility/app/shell/commands/Commands.h>

#include <cstdint>
#include <cstdio>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__) && !defined(ESP_PLATFORM)
// No sysconf(_SC_PHYS_PAGES/_SC_AVPHYS_PAGES) on macOS: total comes from sysctl, free from the
// Mach host VM statistics.
inline uint64_t appleTotalMemory() {
    uint64_t memSize = 0;
    size_t memSizeLen = sizeof(memSize);
    if (sysctlbyname("hw.memsize", &memSize, &memSizeLen, nullptr, 0) != 0) {
        return 0;
    }
    return memSize;
}

inline uint64_t appleFreeMemory() {
    mach_port_t host = mach_host_self();
    vm_size_t pageSize = 0;
    vm_statistics64_data_t vmStats {};
    mach_msg_type_number_t vmStatsCount = HOST_VM_INFO64_COUNT;
    if (host_page_size(host, &pageSize) != KERN_SUCCESS ||
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vmStats), &vmStatsCount) != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<uint64_t>(vmStats.free_count + vmStats.inactive_count) * pageSize;
}
#elif !defined(ESP_PLATFORM)
inline uint64_t pagesToBytes(int pagesParam) {
    const long pages = sysconf(pagesParam);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages < 0 || pageSize < 0) {
        return 0;
    }
    return static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize);
}
#endif

inline uint64_t getHeapTotal() {
#ifdef ESP_PLATFORM
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
#elif defined(__APPLE__)
    return appleTotalMemory();
#else
    return pagesToBytes(_SC_PHYS_PAGES);
#endif
}

inline uint64_t getHeapFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#elif defined(__APPLE__)
    return appleFreeMemory();
#else
    return pagesToBytes(_SC_AVPHYS_PAGES);
#endif
}

int cmdFree(int, char**) {
    puts("                   total            free");
    printf("Heap     %15llu %15llu\n", static_cast<unsigned long long>(getHeapTotal()), static_cast<unsigned long long>(getHeapFree()));
#ifdef ESP_PLATFORM
    printf("External %15zu %15zu\n", heap_caps_get_total_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
    return 0;
}
