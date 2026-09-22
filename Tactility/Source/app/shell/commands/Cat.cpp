#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>

namespace {

/**
 * Emits one chunk of file content.
 *
 * When the destination is the terminal, non-printable bytes become '.', so that cat-ing a binary
 * by mistake doesn't fire escape sequences at the terminal app and leave the screen in a strange
 * state. Redirected output must copy bytes faithfully (`cat a > b`), so this substitution only
 * happens when stdout is the terminal.
 *
 * Line endings are left alone either way: the terminal app translates LF to CRLF itself, at the
 * point where output actually reaches its screen.
 */
bool emitTextChunk(const char* data, size_t length, void* context) {
    auto* lastByte = static_cast<char*>(context);

    if (length > 0) {
        *lastByte = data[length - 1];
    }

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
        out[used++] = printable ? c : '.';
    }

    if (used > 0) {
        printf("%.*s", static_cast<int>(used), reinterpret_cast<const char*>(out));
    }
    return true;
}

} // namespace

int cmdCat(int argc, char** argv) {
    if (argc < 2) {
        // No file: copy stdin through, so `cat` works as the middle of a pipe and `cat < f` works.
        char lastByte = '\n';
        char chunk[256];
        size_t read;
        while ((read = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
            emitTextChunk(chunk, read, &lastByte);
        }
        if (lastByte != '\n') {
            printf("\n");
        }
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        char resolved[ShellFs::MAX_PATH];
        if (!resolveArg("cat", argv[i], resolved, sizeof(resolved))) {
            status = 1;
            continue;
        }

        // Streamed rather than read whole: a large file would otherwise have to fit in the heap
        // all at once just to be printed.
        char lastByte = '\n';
        if (!ShellFs::streamFile(resolved, &lastByte, emitTextChunk)) {
            printf("cat: %s: cannot read\n", argv[i]);
            status = 1;
            continue;
        }

        // Leave the cursor at column zero even when the file has no trailing newline.
        if (lastByte != '\n') {
            printf("\n");
        }
    }
    return status;
}
