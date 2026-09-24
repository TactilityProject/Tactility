#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <tactility/filesystem/file_system.h>
#include <tactility/filesystem/fs.h>

#include <cstdio>

namespace coreutils::ls {

static int32_t main(int argc, char* argv[]) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePathArg("ls", argc > 1 ? argv[1] : "", path, sizeof(path))) {
        return 1;
    }

    if (directory_list(path, nullptr, printEntry) != ERROR_NONE) {
        printf("ls: %s: cannot read\n", path);
        return 1;
    }
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "ls",
    .name = "ls",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::ls
