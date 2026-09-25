#include <app/manifest.h>

#include <cstdio>

namespace coreutils::clear {

static int32_t main(int, char*[]) {
    printf("\x1B[2J\x1B[H");
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "clear",
    .name = "clear",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::clear
