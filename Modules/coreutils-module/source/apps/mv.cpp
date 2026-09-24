#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <tactility/filesystem/fs.h>

#include <cstdio>

namespace coreutils::mv {

static int32_t main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: mv <src> <dst>\n");
        return 1;
    }

    char source[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePathArg("mv", argv[1], source, sizeof(source))) {
        return 1;
    }

    char target[FILE_MAX_PATH_STRING_LENGTH];
    if (!buildTarget(source, argv[2], target, sizeof(target))) {
        printf("mv: path too long\n");
        return 1;
    }

    return reportResult("mv", argv[1], file_move(source, target, true));
}

extern const ::AppManifest manifest = {
    .id = "mv",
    .name = "mv",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::mv
