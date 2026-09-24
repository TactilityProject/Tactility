
#include <app/manifest.h>

#include <cstdio>
#include <ctime>

namespace coreutils::date {

static int32_t main(int, char*[]) {
    const time_t now = time(nullptr);
    tm parts;
    localtime_r(&now, &parts);

    char text[64];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &parts);
    printf("%s\n", text);
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "date",
    .name = "date",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 2048 + 256, .desired_memory_capability = 0 },
};

} // namespace coreutils::date
