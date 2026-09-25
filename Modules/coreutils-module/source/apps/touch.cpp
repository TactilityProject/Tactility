#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <tactility/filesystem/fs.h>

#include <cstdio>

namespace coreutils::touch {

static int32_t main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: touch <file>...\n");
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        char path[FILE_MAX_PATH_STRING_LENGTH];
        if (!resolvePathArg("touch", argv[i], path, sizeof(path))) {
            status = 1;
            continue;
        }
        status |= reportResult("touch", argv[i], file_touch(path));
    }
    return status;
}

extern const ::AppManifest manifest = {
    .id = "touch",
    .name = "touch",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 3072, .desired_memory_capability = 0 },
};

} // namespace coreutils::touch
