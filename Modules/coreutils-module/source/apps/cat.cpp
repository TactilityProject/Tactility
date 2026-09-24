#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <cstdio>

namespace {

/**
 * Emits one chunk of file content.
 *
 * When the destination is the terminal, non-printable bytes become '.', so that cat-ing a binary
 * by mistake doesn't fire escape sequences at the terminal app and leave the screen in a strange
 * state. Redirected output must copy bytes faithfully (`cat a > b`), so this substitution only
 * happens when stdout is the terminal (`context`, set once by the caller).
 *
 * Line endings are left alone either way: the terminal app translates LF to CRLF itself, at the
 * point where output actually reaches its screen.
 */
bool emitTextChunk(const char* data, size_t length, void* context) {
    const bool isTerminal = *static_cast<const bool*>(context);

    char out[512];
    size_t used = 0;

    for (size_t i = 0; i < length; i++) {
        const char c = data[i];

        if (used >= sizeof(out)) {
            printf("%.*s", static_cast<int>(used), reinterpret_cast<const char*>(out));
            used = 0;
        }

        const bool printable = (c == '\n') || (c == '\t') ||
            (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);
        out[used++] = (isTerminal && !printable) ? '.' : c;
    }

    if (used > 0) {
        printf("%.*s", static_cast<int>(used), reinterpret_cast<const char*>(out));
    }
    return true;
}

} // namespace

namespace coreutils::cat {

static int32_t main(int argc, char* argv[]) {
    bool isTerminal = stdoutIsTerminal();

    if (argc < 2) {
        // No file: copy stdin through, so `cat` works as the middle of a pipe and `cat < f` works.
        char chunk[256];
        size_t read;
        while ((read = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
            emitTextChunk(chunk, read, &isTerminal);
        }
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        char path[FILE_MAX_PATH_STRING_LENGTH];
        if (!resolvePathArg("cat", argv[i], path, sizeof(path))) {
            status = 1;
            continue;
        }

        // Streamed rather than read whole: a large file would otherwise have to fit in the heap
        // all at once just to be printed.
        if (!streamFile(path, &isTerminal, emitTextChunk)) {
            fprintf(stderr, "cat: %s: cannot read\n", argv[i]);
            status = 1;
            continue;
        }
    }
    return status;
}

extern const ::AppManifest manifest = {
    .id = "cat",
    .name = "cat",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 5120, .desired_memory_capability = 0 },
};

} // namespace coreutils::cat
