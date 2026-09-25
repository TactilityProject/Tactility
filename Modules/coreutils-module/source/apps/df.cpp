#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <tactility/filesystem/file_system.h>

#include <cstdio>

namespace coreutils::df {

static bool printMount(FileSystem* fs, void*) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (file_system_get_path(fs, path, sizeof(path)) == ERROR_NONE) {
        printf("%s%s%s\n", color(COLOUR_DIR), path, color(COLOUR_RESET));
    }
    return true;
}

static int32_t main(int, char*[]) {
    // Only mount points are listed: ESP-IDF's VFS has no statvfs and Tactility's FileSystem API
    // exposes paths but not capacity, so there is no honest way to report free space here.
    printf("Mounted filesystems:\n");
    file_system_for_each_mounted(nullptr, printMount);
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "df",
    .name = "df",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::df
