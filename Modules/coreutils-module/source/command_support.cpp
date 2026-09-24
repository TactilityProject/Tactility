#include <coreutils/command_support.h>

#include <app/dir.h>
#include <app/io.h>

#include <tactility/paths.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Reads the calling app instance's own cwd (app/dir.h) - inherited from whichever instance
// started this one (e.g. the interactive shell, after a `cd`), so this always agrees with `pwd`.
// "/" if unset/unavailable (matches this module's old hardcoded default).
const char* currentDirectory() {
    static char buffer[FILE_MAX_PATH_STRING_LENGTH];
    if (app_dir_get_cwd(buffer, sizeof(buffer)) != ERROR_NONE) {
        return "/";
    }
    return buffer;
}

/**
 * Collapses `.` and `..` segments in an absolute path, in place.
 *
 * Scanned manually rather than with strtok: strtok keeps static state across calls and is not
 * reentrant, which is the wrong shape for something more than one coreutil app might call.
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

// Module-local equivalent of ShellFs::resolvePath(): resolves against this app instance's own
// cwd (currentDirectory(), above) - inherited from whichever instance started it.
bool resolvePath(const char* path, char* out, size_t outSize) {
    const char* current = currentDirectory();

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

} // namespace

bool isRoot(const char* resolved) {
    return resolved != nullptr && strcmp(resolved, "/") == 0;
}

bool resolvePathArg(const char* command, const char* path, char* out, size_t outSize) {
    if (!resolvePath(path, out, outSize)) {
        printf("%s: %s: path too long\n", command, path);
        return false;
    }
    return true;
}

int reportResult(const char* command, const char* subject, error_t result) {
    if (result == ERROR_NONE) {
        return 0;
    }
    printf("%s: %s: %s\n", command, subject, error_to_string(result));
    return 1;
}

bool stdoutIsTerminal() {
    AppWindowSize size {};
    return app_io_ioctl(STDOUT_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &size) == ERROR_NONE;
}

const char* const COLOUR_RESET = "\x1B[0m";
const char* const COLOUR_DIR = "\x1B[94m";      // bright blue
const char* const COLOUR_EXEC = "\x1B[92m";     // bright green
const char* const COLOUR_SIZE = "\x1B[90m";     // grey, so it recedes behind the names
const char* const COLOUR_ERROR = "\x1B[91m";    // bright red

const char* color(const char* sequence) {
    return stdoutIsTerminal() ? sequence : "";
}

bool buildTarget(const char* sourcePath, const char* targetArg, char* out, size_t outSize) {
    char resolvedTarget[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePath(targetArg, resolvedTarget, sizeof(resolvedTarget))) {
        return false;
    }

    if (!directory_exists(resolvedTarget)) {
        snprintf(out, outSize, "%s", resolvedTarget);
        return true;
    }

    const char* base = strrchr(sourcePath, '/');
    base = (base != nullptr) ? base + 1 : sourcePath;

    const int written = snprintf(out, outSize, "%s/%s", resolvedTarget, base);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

int parseLineCount(int argc, char** argv, int* outCount) {
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        *outCount = atoi(argv[2]);
        return 3;
    }
    return 1;
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
