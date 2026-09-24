#include <coreutils/command_support.h>

#include <app/io.h>
#include <app/manifest.h>

#include <tactility/filesystem/fs.h>

#include <TactilityCpp/Allocator.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace coreutils::ls {

namespace {

struct Entry {
    std::string name;
    bool is_directory;
};

// Prefers PSRAM, like help.cpp's own command list: a directory listing isn't on any hot path.
using EntryList = std::vector<Entry, tt::OptExternalAllocator<Entry>>;

bool looksExecutable(const char* name) {
    const char* dot = strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }
    // TODO: Consider using app_is_executable
    return strcmp(dot, ".sh") == 0 || strcmp(dot, ".elf") == 0 || strcmp(dot, ".so") == 0;
}

void collectEntry(const DirectoryEntry* entry, void* context) {
    static_cast<EntryList*>(context)->push_back({ entry->name, entry->is_directory });
}

void printEntry(const DirectoryEntry* entry, void*) {
    if (entry->is_directory) {
        printf("%s%s/%s\n", COLOUR_DIR, entry->name, COLOUR_RESET);
        return;
    }

    const char* nameColour = looksExecutable(entry->name) ? COLOUR_EXEC : "";
    const char* nameReset = looksExecutable(entry->name) ? COLOUR_RESET : "";

    /*
     * The padding is applied to the name alone rather than to the coloured string: the escape
     * sequences are zero-width on screen but count towards printf's field width, so a coloured
     * "%-24s" would come out short by however many bytes the colour codes take.
     */
    char padded[64];
    snprintf(padded, sizeof(padded), "%-24s", entry->name);

    printf("%s%s%s %s%u%s\n",
                 nameColour, padded, nameReset,
                 COLOUR_SIZE, (unsigned)entry->size, COLOUR_RESET);
}
void printColumns(const EntryList& entries) {
    int nameWidth = 0;
    for (const auto& entry : entries) {
        const int width = static_cast<int>(entry.name.size()) + (entry.is_directory ? 1 : 0);
        if (width > nameWidth) {
            nameWidth = width;
        }
    }

    AppWindowSize windowSize {};
    int columns = 0;
    if (app_io_ioctl(STDIN_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &windowSize) == ERROR_NONE ||
        app_io_ioctl(STDOUT_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &windowSize) == ERROR_NONE ||
        app_io_ioctl(STDERR_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &windowSize) == ERROR_NONE) {
        columns = windowSize.columns;
    }

    // Sized off the widest entry, not a fixed guess, and capped at the window width so one long
    // name can't force perLine below 1.
    const int entryWidth = (columns < nameWidth ? columns : nameWidth) + 1;
    const int perLine = (columns >= entryWidth) ? columns / entryWidth : 1;

    for (size_t i = 0; i < entries.size(); i++) {
        const auto& entry = entries[i];
        const bool exec = !entry.is_directory && looksExecutable(entry.name.c_str());
        const char* nameColour = entry.is_directory ? COLOUR_DIR : (exec ? COLOUR_EXEC : "");
        const char* nameReset = (entry.is_directory || exec) ? COLOUR_RESET : "";

        std::string displayName = entry.name;
        if (entry.is_directory) {
            displayName += "/";
        }

        const bool lastInLine = (i + 1) % perLine == 0 || i + 1 == entries.size();
        if (lastInLine) {
            printf("%s%s%s\n", nameColour, displayName.c_str(), nameReset);
        } else {
            char padded[FILE_MAX_PATH_STRING_LENGTH];
            snprintf(padded, sizeof(padded), "%-*s", entryWidth, displayName.c_str());
            printf("%s%s%s", nameColour, padded, nameReset);
        }
    }
}

} // namespace

static int32_t main(int argc, char* argv[]) {
    const bool longFormat = argc > 1 && strcmp(argv[1], "-l") == 0;
    const int pathArgIndex = longFormat ? 2 : 1;

    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePathArg("ls", argc > pathArgIndex ? argv[pathArgIndex] : "", path, sizeof(path))) {
        return 1;
    }

    if (longFormat) {
        if (directory_list(path, nullptr, printEntry) != ERROR_NONE) {
            printf("ls: %s: cannot read\n", path);
            return 1;
        }
        return 0;
    }

    EntryList entries;
    if (directory_list(path, &entries, collectEntry) != ERROR_NONE) {
        printf("ls: %s: cannot read\n", path);
        return 1;
    }
    printColumns(entries);
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
