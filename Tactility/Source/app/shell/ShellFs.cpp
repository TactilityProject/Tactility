#include <Tactility/app/shell/ShellFs.h>

#include <app/dir.h>
#include <app/paths.h>

#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>

#include <sys/stat.h>

#include <cstdio>
#include <cstring>

constexpr auto* TAG = "ShellFs";

constexpr auto* APP_ID = "tactility.shell";

namespace {

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
    char rebuilt[FILE_MAX_PATH_STRING_LENGTH];
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
    char dataPath[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(dataPath, sizeof(dataPath)) == ERROR_NONE && app_dir_set_cwd(dataPath) == ERROR_NONE) {
        return;
    }
    app_dir_set_cwd("/");
}

const char* cwd() {
    // The shell's cwd lives in app-module (app/dir.h), inherited by every app instance the shell
    // starts (coreutils included), so `cd` here is what keeps `pwd` and e.g. bare `ls` agreeing.
    static char buffer[FILE_MAX_PATH_STRING_LENGTH];
    if (app_dir_get_cwd(buffer, sizeof(buffer)) != ERROR_NONE) {
        return "/";
    }
    return buffer;
}

bool resolvePath(const char* path, char* out, size_t outSize) {
    const char* current = cwd();

    if (path == nullptr || path[0] == '\0') {
        snprintf(out, outSize, "%s", current);
        return true;
    }

    int written;
    if (path[0] == '/') {
        written = snprintf(out, outSize, "%s", path);
    } else if (strcmp(current, "/") == 0) {
        written = snprintf(out, outSize, "/%s", path);
    } else {
        written = snprintf(out, outSize, "%s/%s", current, path);
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

bool changeDirectory(const char* path) {
    char resolved[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePath(path, resolved, sizeof(resolved))) {
        return false;
    }
    return app_dir_set_cwd(resolved) == ERROR_NONE;
}

} // namespace ShellFs
