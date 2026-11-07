// SPDX-License-Identifier: Apache-2.0
#include <tactility/filesystem/file.h>
#include <tactility/filesystem/path.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

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

    // Source and target are the same, but don't have the same name:
    struct stat source_info, target_info;
    if (
        stat(source, &source_info) == 0 &&
        stat(target, &target_info) == 0 &&
        source_info.st_ino != 0 &&
        source_info.st_dev == target_info.st_dev &&
        source_info.st_ino == target_info.st_ino
    ) {
        return ERROR_INVALID_ARGUMENT;
    }

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

} // extern "C"
