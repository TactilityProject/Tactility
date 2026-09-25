#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <tactility/filesystem/file_system.h>
#include <tactility/filesystem/fs.h>

#include <cstdio>

namespace coreutils::du {

static int32_t main(int argc, char* argv[]) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePathArg("du", argc > 1 ? argv[1] : "", path, sizeof(path))) {
        return 1;
    }

    const uint64_t bytes = path_tree_size(path);
    printf("%u\t%s\n", (unsigned)bytes, path);
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "du",
    .name = "du",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 5120, .desired_memory_capability = 0 },
};

} // namespace coreutils::du
