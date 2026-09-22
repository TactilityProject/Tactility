#include <Tactility/app/shell/commands/Commands.h>

#include <tactility/memory.h>

#include <cstdio>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

int cmdFree(int /*unused*/, char** /*unused*/) {
    puts("                   total            free");
    printf("Heap     %15zu %15zu\n", memory_heap_total(), memory_heap_free());
#ifdef ESP_PLATFORM
    printf("External %15zu %15zu\n", heap_caps_get_total_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
    return 0;
}
