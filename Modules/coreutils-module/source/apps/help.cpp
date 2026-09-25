#include <app/io.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <TactilityCpp/Allocator.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace coreutils::help {

namespace {

// Prefers PSRAM over internal RAM, like the rest of app-module's own unbounded collections
// (AppLedger::instances/packages, AppEnv) - this isn't on any hot path, so there's no reason to
// spend scarce internal RAM on it.
using IdList = std::vector<std::string, tt::OptExternalAllocator<std::string>>;

// app_manager_for_each_manifest() holds app_ledger()'s mutex for the whole call and its own
// contract forbids calling back into anything that touches the ledger from inside the visitor -
// printf() is wrapped on some platforms to route through the app I/O layer, which needs the
// ledger too, so this only copies the id out; printing happens after the call returns below.
void collectHeadlessId(const AppManifest* manifest, void* context) {
    if ((manifest->flags & APP_MANIFEST_FLAG_HEADLESS) == 0) {
        return;
    }
    static_cast<IdList*>(context)->emplace_back(manifest->id);
}

} // namespace

static int32_t main(int, char*[]) {
    IdList ids;
    app_manager_for_each_manifest(collectHeadlessId, &ids);

    std::sort(ids.begin(), ids.end());

    int commandWidth = 0;
    for (const auto& id : ids) {
        const int length = static_cast<int>(id.size());
        if (length > commandWidth) {
            commandWidth = length;
        }
    }

    AppWindowSize windowSize {};
    int columns = 0;
    if (app_io_ioctl(STDIN_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &windowSize) == ERROR_NONE ||
        app_io_ioctl(STDOUT_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &windowSize) == ERROR_NONE ||
        app_io_ioctl(STDERR_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &windowSize) == ERROR_NONE) {
        columns = windowSize.columns;
    }

    // Sized off the widest registered id, not the worst-case APP_MANIFEST_ID_LENGTH, so short ids
    // pack tighter; capped at the window width so one long id can't force perLine below 1.
    const int entryWidth = (columns < commandWidth ? columns : commandWidth) + 1;
    const int perLine = (columns >= entryWidth) ? columns / entryWidth : 1;

    puts("Commands:");
    for (size_t i = 0; i < ids.size(); i++) {
        const bool lastInLine = (i + 1) % perLine == 0 || i + 1 == ids.size();
        if (lastInLine) {
            printf("%s\n", ids[i].c_str());
        } else {
            printf("%-*s", entryWidth, ids[i].c_str());
        }
    }
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "help",
    .name = "help",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::help
