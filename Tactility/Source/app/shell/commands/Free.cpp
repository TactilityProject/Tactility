#include <Tactility/app/shell/commands/Commands.h>

#include <cstdint>
#include <cstdio>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#ifndef ESP_PLATFORM
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
#else
    return pagesToBytes(_SC_PHYS_PAGES);
#endif
}

inline uint64_t getHeapFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
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
