#include <Tactility/app/shell/commands/Commands.h>

#include <cstdio>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#else
#include <sys/sysinfo.h>
#endif

inline size_t getHeapTotal() {
#ifdef ESP_PLATFORM
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
#else
    return sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
#endif
}

inline size_t getHeapFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#else
    return sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE);
#endif
}

int cmdFree(int, char**) {
    puts("                   total            free");
    printf("Heap     %15zu %15zu\n", getHeapTotal(), getHeapFree());
#ifdef ESP_PLATFORM
    printf("External %15zu %15zu\n", heap_caps_get_total_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
    return 0;
}
