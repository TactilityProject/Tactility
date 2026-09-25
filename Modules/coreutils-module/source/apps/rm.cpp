#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <tactility/filesystem/fs.h>

#include <cstdio>
#include <cstring>

namespace coreutils::rm {

static int32_t main(int argc, char* argv[]) {
    bool recursive = false;
    int first = 1;
    if (argc > 1 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rf") == 0)) {
        recursive = true;
        first = 2;
    }

    if (first >= argc) {
        printf("usage: rm [-r] <file>...\n");
        return 1;
    }

    int status = 0;
    for (int i = first; i < argc; i++) {
        char path[FILE_MAX_PATH_STRING_LENGTH];
        if (!resolvePathArg("rm", argv[i], path, sizeof(path))) {
            status = 1;
            continue;
        }

        error_t result;
        if (directory_exists(path)) {
            result = recursive ? directory_remove_tree(path) : directory_remove(path);
        } else {
            result = file_remove(path);
        }
        status |= reportResult("rm", argv[i], result);
    }
    return status;
}

extern const ::AppManifest manifest = {
    .id = "rm",
    .name = "rm",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 3072, .desired_memory_capability = 0 },
};

} // namespace coreutils::rm
