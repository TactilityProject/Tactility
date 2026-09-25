#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <tactility/filesystem/fs.h>

#include <cstdio>
#include <cstring>

namespace coreutils::mkdir {

static int32_t main(int argc, char* argv[]) {
    bool createParents = false;
    int first = 1;
    if (argc > 1 && strcmp(argv[1], "-p") == 0) {
        createParents = true;
        first = 2;
    }

    if (first >= argc) {
        printf("usage: mkdir [-p] <dir>...\n");
        return 1;
    }

    int status = 0;
    for (int i = first; i < argc; i++) {
        char path[FILE_MAX_PATH_STRING_LENGTH];
        if (!resolvePathArg("mkdir", argv[i], path, sizeof(path))) {
            status = 1;
            continue;
        }
        status |= reportResult("mkdir", argv[i], directory_make(path, createParents));
    }
    return status;
}

extern const ::AppManifest manifest = {
    .id = "mkdir",
    .name = "mkdir",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 3072, .desired_memory_capability = 0 },
};

} // namespace coreutils::mkdir
