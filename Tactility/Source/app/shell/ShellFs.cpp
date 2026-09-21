#include <Tactility/app/shell/ShellFs.h>

#include <app/paths.h>

#include <tactility/filesystem/file_system.h>

#include <tactility/log.h>
#include <tactility/freertos/freertos.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "ShellFs";

/** Must match manifest.properties' app.id. */
constexpr auto* APP_ID = "tactility.breezybox";

namespace {

char currentDirectory[ShellFs::MAX_PATH] = "/";

struct MountCollector {
    void* context;
    void (*callback)(const char* path, void* context);
};

bool collectMount(FileSystem* fs, void* context) {
    auto* collector = static_cast<MountCollector*>(context);

    if (!file_system_is_mounted(fs)) {
        return true;
    }

    char path[ShellFs::MAX_PATH];
    if (file_system_get_path(fs, path, sizeof(path)) == ERROR_NONE) {
        collector->callback(path, collector->context);
    }
    return true;
}

struct FirstMount {
    char path[ShellFs::MAX_PATH];
    bool found;
};

void takeFirstMount(const char* path, void* context) {
    auto* first = static_cast<FirstMount*>(context);
    if (!first->found) {
        snprintf(first->path, sizeof(first->path), "%s", path);
        first->found = true;
    }
}

/**
 * Collapses `.` and `..` segments in an absolute path, in place.
 *
 * Scanned manually rather than with strtok: strtok keeps static state across calls and is not
 * reentrant, which is the wrong shape for something the shell may call from more than one place.
 */
void normalizePath(char* path) {
    struct Segment {
        const char* start;
        size_t length;
    };
    Segment segments[32];
    int depth = 0;

    const char* p = path;
    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char* start = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
        const size_t length = static_cast<size_t>(p - start);

        if (length == 1 && start[0] == '.') {
            continue;
        }
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            if (depth > 0) {
                depth--;
            }
            continue;
        }
        if (depth < static_cast<int>(sizeof(segments) / sizeof(segments[0]))) {
            segments[depth++] = Segment { start, length };
        }
    }

    // Rebuild into a scratch buffer first: the segments still point into `path`.
    char rebuilt[ShellFs::MAX_PATH];
    size_t offset = 0;
    for (int i = 0; i < depth && offset + 1 < sizeof(rebuilt); i++) {
        rebuilt[offset++] = '/';
        size_t copy = segments[i].length;
        if (offset + copy >= sizeof(rebuilt)) {
            copy = sizeof(rebuilt) - offset - 1;
        }
        memcpy(&rebuilt[offset], segments[i].start, copy);
        offset += copy;
    }
    if (offset == 0) {
        rebuilt[offset++] = '/';
    }
    rebuilt[offset] = '\0';

    memcpy(path, rebuilt, offset + 1);
}

} // namespace

namespace ShellFs {

bool appDataPath(char* buf, size_t* size) {
    if (app_paths_get_user_data_directory(APP_ID, buf, *size) != ERROR_NONE) {
        return false;
    }
    // The directory is not guaranteed to exist yet; creating it here keeps callers simple.
    mkdir(buf, 0755);
    return true;
}

void init() {
    FirstMount first { "/", false };
    forEachMount(&first, takeFirstMount);
    snprintf(currentDirectory, sizeof(currentDirectory), "%s", first.found ? first.path : "/");
    // Deliberately not logged: the log backend writes to stdout, which is piped to the terminal app
    // running this one, so anything logged from here appears on screen as though the shell had
    // printed it. The remaining LOG_W calls in this file report real faults, which is worth that
    // intrusion.
}

const char* cwd() {
    return currentDirectory;
}

bool resolvePath(const char* path, char* out, size_t outSize) {
    if (path == nullptr || path[0] == '\0') {
        snprintf(out, outSize, "%s", currentDirectory);
        return true;
    }

    int written;
    if (path[0] == '/') {
        written = snprintf(out, outSize, "%s", path);
    } else if (strcmp(currentDirectory, "/") == 0) {
        written = snprintf(out, outSize, "/%s", path);
    } else {
        written = snprintf(out, outSize, "%s/%s", currentDirectory, path);
    }

    if (written < 0 || static_cast<size_t>(written) >= outSize) {
        return false;
    }

    normalizePath(out);

    // Trailing slashes confuse stat() on some filesystems; drop all but a lone root.
    size_t length = strlen(out);
    while (length > 1 && out[length - 1] == '/') {
        out[--length] = '\0';
    }
    return true;
}

bool isRoot(const char* resolved) {
    return resolved != nullptr && strcmp(resolved, "/") == 0;
}

void forEachMount(void* context, void (*callback)(const char* path, void* context)) {
    MountCollector collector { context, callback };
    file_system_for_each(&collector, collectMount);
}

bool isDirectory(const char* resolvedPath) {
    if (isRoot(resolvedPath)) {
        return true;
    }

    struct stat info;
    return stat(resolvedPath, &info) == 0 && S_ISDIR(info.st_mode);
}

bool changeDirectory(const char* path) {
    char resolved[MAX_PATH];
    if (!resolvePath(path, resolved, sizeof(resolved))) {
        return false;
    }
    if (!isDirectory(resolved)) {
        return false;
    }
    snprintf(currentDirectory, sizeof(currentDirectory), "%s", resolved);
    return true;
}

char* readFile(const char* resolvedPath, size_t* outSize) {
    FILE* file = fopen(resolvedPath, "rb");
    if (file == nullptr) {
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return nullptr;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(size) + 1));
    if (buffer == nullptr) {
        fclose(file);
        return nullptr;
    }

    const size_t read = fread(buffer, 1, static_cast<size_t>(size), file);
    fclose(file);

    buffer[read] = '\0';
    if (outSize != nullptr) {
        *outSize = read;
    }
    return buffer;
}

bool exists(const char* resolvedPath) {
    if (isRoot(resolvedPath)) {
        return true;
    }

    struct stat info;
    return stat(resolvedPath, &info) == 0;
}

bool bundledBinaryDir(char* out, size_t outSize) {
    return app_paths_get_assets_path(APP_ID, "bin", out, outSize) == ERROR_NONE;
}

bool bundledBinaryPath(const char* name, char* out, size_t outSize) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    // A bare name only: anything with a separator is a path and belongs to the caller's own lookup.
    if (strchr(name, '/') != nullptr) {
        return false;
    }

    // Try the name as given, then with .elf appended, so both `grep` and `grep.elf` work.
    const char* suffixes[] = { "", ".elf" };
    for (const char* suffix : suffixes) {
        char relative[128];
        const int written = snprintf(relative, sizeof(relative), "bin/%s%s", name, suffix);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(relative)) {
            continue;
        }

        if (app_paths_get_assets_path(APP_ID, relative, out, outSize) == ERROR_NONE && exists(out)) {
            return true;
        }
    }

    out[0] = '\0';
    return false;
}

bool isElf(const char* resolvedPath) {
    FILE* file = fopen(resolvedPath, "rb");
    if (file == nullptr) {
        return false;
    }

    unsigned char magic[4] = {};
    const size_t read = fread(magic, 1, sizeof(magic), file);
    fclose(file);

    return read == sizeof(magic) &&
        magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

bool streamFile(const char* resolvedPath, void* context, bool (*callback)(const char*, size_t, void*)) {
    FILE* file = fopen(resolvedPath, "rb");
    if (file == nullptr) {
        return false;
    }

    char chunk[512];
    for (;;) {
        const size_t read = fread(chunk, 1, sizeof(chunk), file);
        if (read == 0) {
            break;
        }
        if (!callback(chunk, read, context)) {
            break;
        }
    }

    fclose(file);
    return true;
}

bool listDirectory(const char* resolvedPath, void* context, void (*callback)(const Entry&, void*)) {
    DIR* dir = opendir(resolvedPath);
    if (dir == nullptr) {
        return false;
    }

    for (struct dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char childPath[MAX_PATH];
        const int written = (strcmp(resolvedPath, "/") == 0)
            ? snprintf(childPath, sizeof(childPath), "/%s", entry->d_name)
            : snprintf(childPath, sizeof(childPath), "%s/%s", resolvedPath, entry->d_name);

        Entry item { entry->d_name, false, 0 };
        if (written > 0 && static_cast<size_t>(written) < sizeof(childPath)) {
            struct stat info;
            if (stat(childPath, &info) == 0) {
                item.isDirectory = S_ISDIR(info.st_mode);
                item.size = static_cast<size_t>(info.st_size);
            }
        }

        callback(item, context);
    }

    closedir(dir);
    return true;
}

const char* describe(Result result) {
    switch (result) {
        case Result::Ok: return "ok";
        case Result::NotFound: return "no such file or directory";
        case Result::Locked: return "filesystem busy";
        case Result::Exists: return "already exists";
        case Result::NotEmpty: return "directory not empty";
        case Result::IoError: return "I/O error";
    }
    return "unknown error";
}

Result makeDirectory(const char* resolvedPath, bool createParents) {
    if (!createParents) {
        if (mkdir(resolvedPath, 0755) != 0) {
            return (errno == EEXIST) ? Result::Exists : Result::IoError;
        }
        return Result::Ok;
    }

    // Walk the path creating each component, temporarily truncating at every separator.
    char working[MAX_PATH];
    snprintf(working, sizeof(working), "%s", resolvedPath);

    for (char* p = working + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        // EEXIST is expected for every parent that is already there.
        if (mkdir(working, 0755) != 0 && errno != EEXIST) {
            return Result::IoError;
        }
        *p = '/';
    }

    if (mkdir(working, 0755) != 0 && errno != EEXIST) {
        return Result::IoError;
    }
    return Result::Ok;
}

Result removeFile(const char* resolvedPath) {
    if (unlink(resolvedPath) != 0) {
        return (errno == ENOENT) ? Result::NotFound : Result::IoError;
    }
    return Result::Ok;
}

Result removeDirectory(const char* resolvedPath) {
    if (rmdir(resolvedPath) != 0) {
        if (errno == ENOENT) {
            return Result::NotFound;
        }
        return (errno == ENOTEMPTY || errno == EEXIST) ? Result::NotEmpty : Result::IoError;
    }
    return Result::Ok;
}

namespace {

// Bound on how deep removeTree() will recurse. The shell task has a fixed stack and each level
// costs a MAX_PATH buffer, so a pathological tree is refused rather than overflowing it.
constexpr int MAX_RECURSION_DEPTH = 16;

// Entries collected per directory pass, and the longest filename handled. dirent allows 255-byte
// names; storing them at full width would cost 16KB per recursion level, so the cap is lower and
// over-long names are reported rather than silently mishandled.
constexpr int MAX_ENTRIES_PER_PASS = 64;
constexpr size_t MAX_ENTRY_NAME = 64;

Result removeTreeAt(const char* path, int depth) {
    if (depth > MAX_RECURSION_DEPTH) {
        return Result::IoError;
    }

    // Names are capped at MAX_ENTRY_NAME rather than dirent's full 256 bytes, which would make this
    // a 16KB allocation per recursion level. Anything longer is skipped rather than stored
    // truncated: a truncated name resolves to a *different* path, and deleting the wrong file is a
    // far worse outcome than refusing to delete an unusually named one.
    struct Collected {
        char names[MAX_ENTRIES_PER_PASS][MAX_ENTRY_NAME];
        int count;
        bool overflowed;
        bool skippedLongName;
    };

    auto* collected = static_cast<Collected*>(calloc(1, sizeof(Collected)));
    if (collected == nullptr) {
        return Result::IoError;
    }

    {
        DIR* dir = opendir(path);
        if (dir == nullptr) {
            free(collected);
            // Not a directory: fall through to removing it as a plain file.
            return removeFile(path);
        }

        for (struct dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (collected->count >= MAX_ENTRIES_PER_PASS) {
                collected->overflowed = true;
                break;
            }

            const size_t nameLength = strlen(entry->d_name);
            if (nameLength >= MAX_ENTRY_NAME) {
                collected->skippedLongName = true;
                continue;
            }

            memcpy(collected->names[collected->count], entry->d_name, nameLength + 1);
            collected->count++;
        }
        closedir(dir);
    }

    Result result = Result::Ok;
    for (int i = 0; i < collected->count; i++) {
        char childPath[MAX_PATH];
        snprintf(childPath, sizeof(childPath), "%s/%s", path, collected->names[i]);

        const Result childResult = isDirectory(childPath)
            ? removeTreeAt(childPath, depth + 1)
            : removeFile(childPath);

        if (childResult != Result::Ok) {
            result = childResult;
        }
    }

    const bool overflowed = collected->overflowed;
    const bool skippedLongName = collected->skippedLongName;
    free(collected);

    // A directory with more entries than one pass could hold needs another sweep.
    if (overflowed && result == Result::Ok && !skippedLongName) {
        return removeTreeAt(path, depth);
    }
    if (result != Result::Ok) {
        return result;
    }
    if (skippedLongName) {
        // The directory still holds entries this code declined to touch, so removing it would fail
        // anyway - report the real reason rather than a bare "not empty".
        LOG_W(TAG, "'%s' contains a name longer than %u bytes; not removed",
                 path, (unsigned)MAX_ENTRY_NAME);
        return Result::NotEmpty;
    }

    return removeDirectory(path);
}

} // namespace

Result removeTree(const char* resolvedPath) {
    return removeTreeAt(resolvedPath, 0);
}

Result copyFile(const char* resolvedSource, const char* resolvedTarget, bool overwrite) {
    // Defined below; a zero-length source is completed by creating an empty target.
    Result touchFile(const char* resolvedPath);

    if (!overwrite && exists(resolvedTarget)) {
        return Result::Exists;
    }

    // Copies in chunks, carrying the file offset between them, since source and target can live
    // on different mounts.
    long offset = 0;
    for (;;) {
        char chunk[512];
        size_t read = 0;

        {
            FILE* source = fopen(resolvedSource, "rb");
            if (source == nullptr) {
                return Result::NotFound;
            }
            if (fseek(source, offset, SEEK_SET) != 0) {
                fclose(source);
                return Result::IoError;
            }
            read = fread(chunk, 1, sizeof(chunk), source);
            fclose(source);
        }

        if (read == 0) {
            break;
        }

        {
            // "wb" for the first chunk so an overwrite truncates; "ab" to extend after that.
            FILE* target = fopen(resolvedTarget, (offset == 0) ? "wb" : "ab");
            if (target == nullptr) {
                return Result::IoError;
            }
            const size_t written = fwrite(chunk, 1, read, target);
            fclose(target);
            if (written != read) {
                return Result::IoError;
            }
        }

        offset += static_cast<long>(read);

        if (read < sizeof(chunk)) {
            break;
        }
    }

    // A zero-length source never entered the write branch above, so create the target explicitly.
    if (offset == 0) {
        return touchFile(resolvedTarget);
    }
    return Result::Ok;
}

Result moveFile(const char* resolvedSource, const char* resolvedTarget, bool overwrite) {
    if (!overwrite && exists(resolvedTarget)) {
        return Result::Exists;
    }

    {
        if (rename(resolvedSource, resolvedTarget) == 0) {
            return Result::Ok;
        }
    }

    // rename() only works within a filesystem; across mounts it fails with EXDEV and the move has
    // to be done the long way.
    const Result copied = copyFile(resolvedSource, resolvedTarget, overwrite);
    if (copied != Result::Ok) {
        return copied;
    }
    return removeFile(resolvedSource);
}

Result touchFile(const char* resolvedPath) {
    // "ab" creates the file when missing and leaves existing content alone.
    FILE* file = fopen(resolvedPath, "ab");
    if (file == nullptr) {
        return Result::IoError;
    }
    fclose(file);
    return Result::Ok;
}

Result writeFile(const char* resolvedPath, const char* data, size_t length, bool append) {
    FILE* file = fopen(resolvedPath, append ? "ab" : "wb");
    if (file == nullptr) {
        return Result::IoError;
    }

    const size_t written = fwrite(data, 1, length, file);
    fclose(file);
    return (written == length) ? Result::Ok : Result::IoError;
}

uint64_t treeSize(const char* resolvedPath) {
    if (!isDirectory(resolvedPath)) {
        struct stat info;
        return (stat(resolvedPath, &info) == 0) ? static_cast<uint64_t>(info.st_size) : 0;
    }

    struct Accumulator {
        uint64_t bytes;
    };
    Accumulator accumulator { 0 };

    listDirectory(resolvedPath, &accumulator, [](const Entry& entry, void* context) {
        // Only files are summed here; directories are recursed into by the caller below.
        if (!entry.isDirectory) {
            static_cast<Accumulator*>(context)->bytes += entry.size;
        }
    });

    // Collect subdirectory names first, then recurse.
    // Same name-length cap as removeTreeAt: a truncated name would size a different path.
    struct Children {
        char names[32][MAX_ENTRY_NAME];
        int count;
    };
    auto* children = static_cast<Children*>(calloc(1, sizeof(Children)));
    if (children == nullptr) {
        return accumulator.bytes;
    }

    listDirectory(resolvedPath, children, [](const Entry& entry, void* context) {
        auto* list = static_cast<Children*>(context);
        if (!entry.isDirectory || list->count >= static_cast<int>(sizeof(list->names) / sizeof(list->names[0]))) {
            return;
        }
        const size_t nameLength = strlen(entry.name);
        if (nameLength >= MAX_ENTRY_NAME) {
            return;
        }
        memcpy(list->names[list->count], entry.name, nameLength + 1);
        list->count++;
    });

    uint64_t total = accumulator.bytes;
    for (int i = 0; i < children->count; i++) {
        char childPath[MAX_PATH];
        snprintf(childPath, sizeof(childPath), "%s/%s", resolvedPath, children->names[i]);
        total += treeSize(childPath);
    }

    free(children);
    return total;
}

} // namespace ShellFs
