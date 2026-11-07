// SPDX-License-Identifier: Apache-2.0
#include <tactility/filesystem/file.h>
#include <tactility/filesystem/path.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

// Compares paths like a case-insensitive file system does, where repeated separators mean one
bool paths_match_ignoring_case(const char* left, const char* right) {
    for (;;) {
        while (*left == '/' && left[1] == '/') {
            left++;
        }
        while (*right == '/' && right[1] == '/') {
            right++;
        }
        if (tolower(static_cast<unsigned char>(*left)) != tolower(static_cast<unsigned char>(*right))) {
            return false;
        }
        if (*left == '\0') {
            return true;
        }
        left++;
        right++;
    }
}

// Whether two paths name the same file. Without inode numbers (e.g. FATFS reports 0) a file
// system can still resolve different names to one file, such as a differently cased one.
// Then the names must match ignoring case, and the size and modification time must match.
bool is_same_file(const char* source, const char* target) {
    struct stat source_info, target_info;
    if (stat(source, &source_info) != 0 || stat(target, &target_info) != 0 || source_info.st_dev != target_info.st_dev) {
        return false;
    }
    if (source_info.st_ino != 0 || target_info.st_ino != 0) {
        return source_info.st_ino == target_info.st_ino;
    }
    return source_info.st_size == target_info.st_size &&
        source_info.st_mtime == target_info.st_mtime &&
        paths_match_ignoring_case(source, target);
}

} // namespace

extern "C" {

bool file_exists(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

error_t file_read_binary(const char* path, uint8_t* bytes, size_t* out_size) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return ERROR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ERROR_RESOURCE;
    }
    const long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ERROR_RESOURCE;
    }

    if (static_cast<size_t>(size) > *out_size) {
        fclose(file);
        return ERROR_BUFFER_OVERFLOW;
    }

    const size_t read = fread(bytes, 1, static_cast<size_t>(size), file);
    const bool failed = ferror(file) != 0;
    fclose(file);

    if (failed || read != static_cast<size_t>(size)) {
        return ERROR_RESOURCE;
    }

    *out_size = read;
    return ERROR_NONE;
}

error_t file_remove(const char* path) {
    if (unlink(path) != 0) {
        return (errno == ENOENT) ? ERROR_NOT_FOUND : ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

error_t file_copy(const char* source, const char* target, bool overwrite) {
    // Reject a copy when the source and target are the same file.
    if (strcmp(source, target) == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    // Source and target are the same, but don't have the same name
    if (is_same_file(source, target)) {
        return ERROR_INVALID_ARGUMENT;
    }

    // Created exclusively when not overwriting, so a target that another task creates meanwhile is never truncated
    bool created = false;
    if (!overwrite) {
        FILE* probe = fopen(source, "rb");
        if (probe == nullptr) {
            return ERROR_NOT_FOUND;
        }
        fclose(probe);

        const int fd = open(target, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            return (errno == EEXIST) ? ERROR_ALREADY_EXISTS : ERROR_RESOURCE;
        }
        close(fd);
        created = true;
    }

    // A target that this call created is removed again when the copy fails
    const auto fail = [&](error_t error) {
        if (created) {
            unlink(target);
        }
        return error;
    };

    // Copies in chunks, carrying the file offset between them, since source and target can live
    // on different mounts.
    long offset = 0;
    for (;;) {
        char chunk[512];
        size_t read = 0;

        {
            FILE* source_file = fopen(source, "rb");
            if (source_file == nullptr) {
                return fail(ERROR_NOT_FOUND);
            }
            if (fseek(source_file, offset, SEEK_SET) != 0) {
                fclose(source_file);
                return fail(ERROR_RESOURCE);
            }
            read = fread(chunk, 1, sizeof(chunk), source_file);
            // A read error ends the read like the end of the file does
            const bool failed = ferror(source_file) != 0;
            fclose(source_file);
            if (failed) {
                return fail(ERROR_RESOURCE);
            }
        }

        if (read == 0) {
            break;
        }

        {
            // "wb" for the first chunk so an overwrite truncates; "ab" to extend after that.
            FILE* target_file = fopen(target, (offset == 0) ? "wb" : "ab");
            if (target_file == nullptr) {
                return fail(ERROR_RESOURCE);
            }
            const size_t written = fwrite(chunk, 1, read, target_file);
            // Buffered output reaches the file at close, so a failure there is a failed copy
            const int close_result = fclose(target_file);
            if (written != read || close_result != 0) {
                return fail(ERROR_RESOURCE);
            }
        }

        offset += static_cast<long>(read);

        if (read < sizeof(chunk)) {
            break;
        }
    }

    // A zero-length source never entered the write branch above, so create or truncate the target explicitly.
    if (offset == 0) {
        FILE* target_file = fopen(target, "wb");
        if (target_file == nullptr) {
            return fail(ERROR_RESOURCE);
        }
        if (fclose(target_file) != 0) {
            return fail(ERROR_RESOURCE);
        }
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

} // extern "C"
