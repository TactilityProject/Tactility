// SPDX-License-Identifier: Apache-2.0
#include <tactility/filesystem/fs.h>

#include <tactility/log.h>
#include <tactility/paths.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "fs";

// Bound on how deep directory_remove_tree()/path_tree_size() will recurse: a fixed-size buffer is
// spent per level, so a pathological tree is refused rather than overflowing the stack.
constexpr int MAX_RECURSION_DEPTH = 16;

// Entries collected per directory pass, and the longest filename handled, for
// directory_remove_tree()/path_tree_size(). dirent allows 255-byte names; storing them at full
// width would cost 16KB per recursion level, so the cap is lower and over-long names are skipped
// rather than silently mishandled.
constexpr int MAX_ENTRIES_PER_PASS = 64;
constexpr size_t MAX_ENTRY_NAME = 64;

namespace {

// Checked before creating, not after: on some mounts (e.g. the SD card's FATFS mount root),
// mkdir() on an already-existing directory doesn't fail with EEXIST, so an mkdir-first/
// errno-check approach never recognizes it as already there (see also file::
// findOrCreateDirectoryInternal() in Tactility/Source/file/File.cpp, which works around the same
// quirk the same way).
bool make_directory(const char* path) {
    struct stat info;
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

error_t directory_remove_tree_at(const char* path, int depth) {
    if (depth > MAX_RECURSION_DEPTH) {
        return ERROR_RESOURCE;
    }

    // Names are capped at MAX_ENTRY_NAME rather than dirent's full 256 bytes, which would make this
    // a 16KB allocation per recursion level. Anything longer is skipped rather than stored
    // truncated: a truncated name resolves to a *different* path, and deleting the wrong file is a
    // far worse outcome than refusing to delete an unusually named one.
    struct Collected {
        char names[MAX_ENTRIES_PER_PASS][MAX_ENTRY_NAME];
        int count;
        bool overflowed;
        bool skipped_long_name;
    };

    auto* collected = static_cast<Collected*>(calloc(1, sizeof(Collected)));
    if (collected == nullptr) {
        return ERROR_RESOURCE;
    }

    {
        DIR* dir = opendir(path);
        if (dir == nullptr) {
            free(collected);
            // Not a directory: fall through to removing it as a plain file.
            return file_remove(path);
        }

        for (struct dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (collected->count >= MAX_ENTRIES_PER_PASS) {
                collected->overflowed = true;
                break;
            }

            const size_t name_length = strlen(entry->d_name);
            if (name_length >= MAX_ENTRY_NAME) {
                collected->skipped_long_name = true;
                continue;
            }

            memcpy(collected->names[collected->count], entry->d_name, name_length + 1);
            collected->count++;
        }
        closedir(dir);
    }

    error_t result = ERROR_NONE;
    for (int i = 0; i < collected->count; i++) {
        char child_path[FILE_MAX_PATH_STRING_LENGTH];
        snprintf(child_path, sizeof(child_path), "%s/%s", path, collected->names[i]);

        const error_t child_result = directory_exists(child_path)
            ? directory_remove_tree_at(child_path, depth + 1)
            : file_remove(child_path);

        if (child_result != ERROR_NONE) {
            result = child_result;
        }
    }

    const bool overflowed = collected->overflowed;
    const bool skipped_long_name = collected->skipped_long_name;
    free(collected);

    // A directory with more entries than one pass could hold needs another sweep.
    if (overflowed && result == ERROR_NONE && !skipped_long_name) {
        return directory_remove_tree_at(path, depth);
    }
    if (result != ERROR_NONE) {
        return result;
    }
    if (skipped_long_name) {
        // The directory still holds entries this code declined to touch, so removing it would fail
        // anyway; report the real reason rather than a bare "not empty".
        LOG_W(TAG, "'%s' contains a name longer than %u bytes; not removed",
                 path, (unsigned)MAX_ENTRY_NAME);
        return ERROR_NOT_EMPTY;
    }

    return directory_remove(path);
}

uint64_t path_tree_size_at(const char* path, int depth) {
    if (depth > MAX_RECURSION_DEPTH) {
        return 0;
    }
    if (!directory_exists(path)) {
        struct stat info;
        return (stat(path, &info) == 0) ? static_cast<uint64_t>(info.st_size) : 0;
    }

    struct Accumulator {
        uint64_t bytes;
    };
    Accumulator accumulator { 0 };

    directory_list(path, &accumulator, [](const DirectoryEntry* entry, void* context) {
        // Only files are summed here; directories are recursed into by the caller below.
        if (!entry->is_directory) {
            static_cast<Accumulator*>(context)->bytes += entry->size;
        }
    });

    // Collect subdirectory names first, then recurse.
    // Same name-length cap as directory_remove_tree_at: a truncated name would size a different path.
    struct Children {
        char names[32][MAX_ENTRY_NAME];
        int count;
    };
    auto* children = static_cast<Children*>(calloc(1, sizeof(Children)));
    if (children == nullptr) {
        return accumulator.bytes;
    }

    directory_list(path, children, [](const DirectoryEntry* entry, void* context) {
        auto* list = static_cast<Children*>(context);
        if (!entry->is_directory || list->count >= static_cast<int>(sizeof(list->names) / sizeof(list->names[0]))) {
            return;
        }
        const size_t name_length = strlen(entry->name);
        if (name_length >= MAX_ENTRY_NAME) {
            return;
        }
        memcpy(list->names[list->count], entry->name, name_length + 1);
        list->count++;
    });

    uint64_t total = accumulator.bytes;
    for (int i = 0; i < children->count; i++) {
        char child_path[FILE_MAX_PATH_STRING_LENGTH];
        snprintf(child_path, sizeof(child_path), "%s/%s", path, children->names[i]);
        total += path_tree_size_at(child_path, depth + 1);
    }

    free(children);
    return total;
}

} // namespace

extern "C" {

bool path_exists(const char* path) {
    struct stat info;
    return stat(path, &info) == 0;
}

bool directory_exists(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

error_t file_read_binary(const char* path, uint8_t* bytes, size_t* out_size) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return ERROR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0 || static_cast<size_t>(size) > *out_size) {
        fclose(file);
        return ERROR_BUFFER_OVERFLOW;
    }

    const size_t read = fread(bytes, 1, static_cast<size_t>(size), file);
    fclose(file);

    *out_size = read;
    return ERROR_NONE;
}

error_t directory_list(const char* path, void* context, void (*callback)(const DirectoryEntry* entry, void* context)) {
    DIR* dir = opendir(path);
    if (dir == nullptr) {
        return ERROR_NOT_FOUND;
    }

    for (struct dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_path[FILE_MAX_PATH_STRING_LENGTH];
        const int written = (strcmp(path, "/") == 0)
            ? snprintf(child_path, sizeof(child_path), "/%s", entry->d_name)
            : snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);

        DirectoryEntry item { entry->d_name, false, 0 };
        if (written > 0 && static_cast<size_t>(written) < sizeof(child_path)) {
            struct stat info;
            if (stat(child_path, &info) == 0) {
                item.is_directory = S_ISDIR(info.st_mode);
                item.size = static_cast<size_t>(info.st_size);
            }
        }

        callback(&item, context);
    }

    closedir(dir);
    return ERROR_NONE;
}

error_t directory_make(const char* path, bool create_parents) {
    if (!create_parents) {
        // Checked before creating; see makeDirectory()'s own comment for why.
        struct stat info;
        if (stat(path, &info) == 0) {
            return ERROR_ALREADY_EXISTS;
        }
        if (mkdir(path, 0755) != 0) {
            return (errno == EEXIST) ? ERROR_ALREADY_EXISTS : ERROR_RESOURCE;
        }
        return ERROR_NONE;
    }

    // Walk the path creating each component, temporarily truncating at every separator.
    char working[FILE_MAX_PATH_STRING_LENGTH];
    snprintf(working, sizeof(working), "%s", path);

    for (char* p = working + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (!make_directory(working)) {
            return ERROR_RESOURCE;
        }
        *p = '/';
    }

    if (!make_directory(working)) {
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

error_t file_remove(const char* path) {
    if (unlink(path) != 0) {
        return (errno == ENOENT) ? ERROR_NOT_FOUND : ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

error_t directory_remove(const char* path) {
    if (rmdir(path) != 0) {
        if (errno == ENOENT) {
            return ERROR_NOT_FOUND;
        }
        return (errno == ENOTEMPTY || errno == EEXIST) ? ERROR_NOT_EMPTY : ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

error_t directory_remove_tree(const char* path) {
    return directory_remove_tree_at(path, 0);
}

error_t file_copy(const char* source, const char* target, bool overwrite) {
    if (!overwrite && path_exists(target)) {
        return ERROR_ALREADY_EXISTS;
    }

    // Copies in chunks, carrying the file offset between them, since source and target can live
    // on different mounts.
    long offset = 0;
    for (;;) {
        char chunk[512];
        size_t read = 0;

        {
            FILE* source_file = fopen(source, "rb");
            if (source_file == nullptr) {
                return ERROR_NOT_FOUND;
            }
            if (fseek(source_file, offset, SEEK_SET) != 0) {
                fclose(source_file);
                return ERROR_RESOURCE;
            }
            read = fread(chunk, 1, sizeof(chunk), source_file);
            fclose(source_file);
        }

        if (read == 0) {
            break;
        }

        {
            // "wb" for the first chunk so an overwrite truncates; "ab" to extend after that.
            FILE* target_file = fopen(target, (offset == 0) ? "wb" : "ab");
            if (target_file == nullptr) {
                return ERROR_RESOURCE;
            }
            const size_t written = fwrite(chunk, 1, read, target_file);
            fclose(target_file);
            if (written != read) {
                return ERROR_RESOURCE;
            }
        }

        offset += static_cast<long>(read);

        if (read < sizeof(chunk)) {
            break;
        }
    }

    // A zero-length source never entered the write branch above, so create the target explicitly.
    if (offset == 0) {
        return file_touch(target);
    }
    return ERROR_NONE;
}

error_t file_move(const char* source, const char* target, bool overwrite) {
    if (!overwrite && path_exists(target)) {
        return ERROR_ALREADY_EXISTS;
    }

    if (rename(source, target) == 0) {
        return ERROR_NONE;
    }

    // rename() only works within a filesystem; across mounts it fails with EXDEV and the move has
    // to be done the long way.
    const error_t copied = file_copy(source, target, overwrite);
    if (copied != ERROR_NONE) {
        return copied;
    }
    return file_remove(source);
}

error_t file_touch(const char* path) {
    // "ab" creates the file when missing and leaves existing content alone.
    FILE* file = fopen(path, "ab");
    if (file == nullptr) {
        return ERROR_RESOURCE;
    }
    fclose(file);
    return ERROR_NONE;
}

uint64_t path_tree_size(const char* path) {
    return path_tree_size_at(path, 0);
}

} // extern "C"
