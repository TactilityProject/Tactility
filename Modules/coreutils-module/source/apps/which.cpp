#include <coreutils/command_support.h>

#include <app/manager.h>
#include <app/manifest.h>

#include <cstdio>

/* Reports where a command comes from: a shell builtin, a bundled binary, or nothing. */
namespace coreutils::which {

static int32_t main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: which <command>...\n");
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        AppManifest manifest {};
        const bool is_registered = app_manager_find_manifest(argv[i], &manifest) == ERROR_NONE
            && (manifest.flags & APP_MANIFEST_FLAG_HEADLESS) != 0;
        if (is_registered) {
            if (manifest.location.type == APP_LOCATION_MEMORY) {
                puts("builtin");
            } else {
                puts(static_cast<const char*>(manifest.location.location));
            }
        } else {
            fprintf(stderr, "which: no %s found\n", argv[i]);
            status = 1;
        }
    }

    return status;
}

extern const ::AppManifest manifest = {
    .id = "which",
    .name = "which",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 3072, .desired_memory_capability = 0 },
};

} // namespace coreutils::which
