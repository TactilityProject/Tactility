#include <app/manifest.h>

#include <tactility/memory.h>

#include <cstdio>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace coreutils::free {

static int32_t main(int /*unused*/, char* /*unused*/[]) {
    puts("                   total            free");
#ifdef ESP_PLATFORM
    printf("Heap     %15zu %15zu\n", memory_heap_total(), memory_heap_free());
    printf("External %15zu %15zu\n", heap_caps_get_total_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#else
    // Not the process's own heap capacity - see memory_heap_total()'s doc comment.
    printf("System   %15zu %15zu\n", memory_heap_total(), memory_heap_free());
#endif
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "free",
    .name = "free",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 3072, .desired_memory_capability = 0 },
};

} // namespace coreutils::free
