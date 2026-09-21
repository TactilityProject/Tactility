#include <Tactility/app/shell/Shell.h>

#include "tactility/memory.h"

#include <Tactility/app/shell/LineEditor.h>
#include <Tactility/app/shell/ShellFs.h>

#include <app/event.h>
#include <app/execute.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <tactility/freertos/freertos.h>
#include <tactility/freertos/task.h>

#include <unistd.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#include <tactility/error.h>

extern "C" {
#include <Tactility/app/shell/sound/snd_core.h>
#include <Tactility/app/shell/sound/snd_port.h>
#include <Tactility/app/shell/shell/sh.h>
}

#include <Tactility/app/shell/ShellBridge.h>

namespace {

// Interpreter state: variables, functions, positional params, $?. Lives for the whole session so
// that `X=1` at the prompt is still set on the next line.
sh_state interpreter;

// stdout as it stands with no redirect active - i.e. piped to the terminal app running this one.
// Captured at init, before any redirect has had a chance to swap it.
FILE* terminalStdout = nullptr;

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

// All command output goes through stdout, which is what makes `ls > out.txt` work: sh_redir.c
// redirects by swapping the stdout FILE*, so this ends up wherever that points - the redirect
// target, or the terminal app running this one, piped in via app_start_for_result_with_streams()
// before this app's task even began. Shell::writeRaw() rather than fputs(): buffered <cstdio>
// output isn't guaranteed to reach the underlying fd (and so the AppStream capturing it) on every
// call - see Shell::writeRaw()'s own doc.
void writeText(const char* text) {
    Shell::writeRaw(text, strlen(text));
}

// ---------------------------------------------------------------------------
// Shared command helpers
// ---------------------------------------------------------------------------

/** Resolves an argument, reporting the failure to the terminal. Returns false if it didn't fit. */
bool resolveArg(const char* command, const char* path, char* out, size_t outSize) {
    if (!ShellFs::resolvePath(path, out, outSize)) {
        Shell::print("%s: %s: path too long\n", command, path);
        return false;
    }
    return true;
}

/** Defined below, next to the `sh` builtin; used by runCommand() to execute `./script.sh`. */
int runScript(const char* resolvedPath, int argc, char** argv);

/** Defined below; used by runCommand() to execute an ELF binary such as `./plasma`. */
int runElf(const char* resolvedPath, int argc, char** argv);

/** Reports a ShellFs::Result if it isn't Ok. Returns the shell exit code. */
int reportResult(const char* command, const char* subject, ShellFs::Result result) {
    if (result == ShellFs::Result::Ok) {
        return 0;
    }
    Shell::print("%s: %s: %s\n", command, subject, ShellFs::describe(result));
    return 1;
}

// ---------------------------------------------------------------------------
// Builtins
// ---------------------------------------------------------------------------




/*
 * Colour, as SGR escape sequences.
 *
 * Only ever emitted when the output is the terminal: a redirect or a pipe must receive plain text,
 * or `ls > files.txt` writes escape sequences into the file and `ls | grep x` matches against them.
 * Shell::outputIsTerminal() is the same guard `cat` uses for its non-printable filtering.
 */
constexpr auto* COLOUR_RESET = "\x1B[0m";
constexpr auto* COLOUR_DIR = "\x1B[94m";      // bright blue
constexpr auto* COLOUR_EXEC = "\x1B[92m";     // bright green
constexpr auto* COLOUR_SIZE = "\x1B[90m";     // grey, so it recedes behind the names
constexpr auto* COLOUR_ERROR = "\x1B[91m";    // bright red

/** Returns the escape sequence, or an empty string when output is not the terminal. */
const char* colour(const char* sequence) {
    return Shell::outputIsTerminal() ? sequence : "";
}

/** True if the name looks like something that can be run. */
bool looksExecutable(const char* name) {
    const char* dot = strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }
    return strcmp(dot, ".elf") == 0 || strcmp(dot, ".sh") == 0;
}

void printMount(const char* path, void*) {
    Shell::print("%s%s%s\n", colour(COLOUR_DIR), path, colour(COLOUR_RESET));
}

void printEntry(const ShellFs::Entry& entry, void*) {
    if (entry.isDirectory) {
        Shell::print("%s%s/%s\n", colour(COLOUR_DIR), entry.name, colour(COLOUR_RESET));
        return;
    }

    const char* nameColour = looksExecutable(entry.name) ? COLOUR_EXEC : "";
    const char* nameReset = looksExecutable(entry.name) ? COLOUR_RESET : "";

    /*
     * The padding is applied to the name alone rather than to the coloured string: the escape
     * sequences are zero-width on screen but count towards printf's field width, so a coloured
     * "%-24s" would come out short by however many bytes the colour codes take.
     */
    char padded[64];
    snprintf(padded, sizeof(padded), "%-24s", entry.name);

    Shell::print("%s%s%s %s%u%s\n",
                 colour(nameColour), padded, colour(nameReset),
                 colour(COLOUR_SIZE), (unsigned)entry.size, colour(COLOUR_RESET));
}

int cmdLs(int argc, char** argv) {
    char resolved[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(argc > 1 ? argv[1] : "", resolved, sizeof(resolved))) {
        writeText("ls: path too long\n");
        return 1;
    }

    // There is no filesystem at "/" - it is synthesised from the list of mount points.
    if (ShellFs::isRoot(resolved)) {
        ShellFs::forEachMount(nullptr, printMount);
        return 0;
    }

    if (!ShellFs::listDirectory(resolved, nullptr, printEntry)) {
        Shell::print("ls: %s: cannot read\n", resolved);
        return 1;
    }
    return 0;
}

/**
 * Emits one chunk of file content.
 *
 * When the destination is the terminal, non-printable bytes become '.', so that cat-ing a binary by
 * mistake doesn't fire escape sequences at the terminal app and leave the screen in a strange
 * state. That is wrong when the output has been redirected — `cat a > b` must copy bytes faithfully
 * — so it is skipped when stdout is no longer the terminal.
 *
 * Line endings are left alone either way: the terminal app translates LF to CRLF itself, at the
 * point where output actually reaches its screen.
 */
bool emitTextChunk(const char* data, size_t length, void* context) {
    auto* lastByte = static_cast<char*>(context);

    if (length > 0) {
        *lastByte = data[length - 1];
    }

    if (!Shell::outputIsTerminal()) {
        fwrite(data, 1, length, stdout);
        return true;
    }

    char out[512];
    size_t used = 0;

    for (size_t i = 0; i < length; i++) {
        const char c = data[i];

        if (used >= sizeof(out)) {
            Shell::writeRaw(out, used);
            used = 0;
        }

        const bool printable = (c == '\n') || (c == '\t') ||
            (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);
        out[used++] = printable ? c : '.';
    }

    if (used > 0) {
        Shell::writeRaw(out, used);
    }
    return true;
}

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
            writeText("\n");
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
            Shell::print("cat: %s: cannot read\n", argv[i]);
            status = 1;
            continue;
        }

        // Leave the cursor at column zero even when the file has no trailing newline.
        if (lastByte != '\n') {
            writeText("\n");
        }
    }
    return status;
}

/**
 * printf: writes the format string with backslash escapes interpreted, substituting arguments.
 *
 * Only the conversions a shell script realistically uses are handled (%s, %d, %%) - this is not a
 * general printf, and the format is never handed to the C library, since a script-supplied format
 * string with an unexpected conversion would read arbitrary stack.
 */
int cmdPrintf(int argc, char** argv) {
    if (argc < 2) {
        writeText("usage: printf <format> [args...]\n");
        return 1;
    }

    int nextArg = 2;

    for (const char* p = argv[1]; *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0') {
            p++;

            // Octal escapes: \033 is how a script writes ESC to start an ANSI colour sequence.
            if (*p >= '0' && *p <= '7') {
                int value = 0;
                int digits = 0;
                while (digits < 3 && *p >= '0' && *p <= '7') {
                    value = value * 8 + (*p - '0');
                    p++;
                    digits++;
                }
                p--; // the loop's own p++ will step past the last digit
                const char text[2] = { static_cast<char>(value), '\0' };
                // Written directly: a NUL would terminate the string, and ESC must reach the
                // terminal intact for the escape sequence to be recognised.
                Shell::writeRaw(text, 1);
                continue;
            }

            switch (*p) {
                case 'n': writeText("\n"); break;
                case 't': writeText("\t"); break;
                case 'r': writeText("\r"); break;
                case 'e': Shell::writeRaw("\x1B", 1); break; // \e, a common shorthand for ESC
                case 'a': break;                               // bell: nothing to ring
                case '\\': writeText("\\"); break;
                default: {
                    const char text[3] = { '\\', *p, '\0' };
                    writeText(text);
                    break;
                }
            }
            continue;
        }

        if (*p == '%' && p[1] != '\0') {
            p++;
            if (*p == '%') {
                writeText("%");
                continue;
            }
            const char* value = (nextArg < argc) ? argv[nextArg++] : "";
            switch (*p) {
                case 's': Shell::print("%s", value); break;
                case 'd':
                case 'i': Shell::print("%d", atoi(value)); break;
                default: {
                    const char text[3] = { '%', *p, '\0' };
                    writeText(text);
                    break;
                }
            }
            continue;
        }

        const char text[2] = { *p, '\0' };
        writeText(text);
    }

    return 0;
}

int cmdClear(int, char**) {
    writeText("\x1B[2J\x1B[H");
    return 0;
}

// ---------------------------------------------------------------------------
// File manipulation
// ---------------------------------------------------------------------------

int cmdMkdir(int argc, char** argv) {
    bool createParents = false;
    int first = 1;
    if (argc > 1 && strcmp(argv[1], "-p") == 0) {
        createParents = true;
        first = 2;
    }

    if (first >= argc) {
        writeText("usage: mkdir [-p] <dir>...\n");
        return 1;
    }

    int status = 0;
    for (int i = first; i < argc; i++) {
        char resolved[ShellFs::MAX_PATH];
        if (!resolveArg("mkdir", argv[i], resolved, sizeof(resolved))) {
            status = 1;
            continue;
        }
        status |= reportResult("mkdir", argv[i], ShellFs::makeDirectory(resolved, createParents));
    }
    return status;
}

int cmdRm(int argc, char** argv) {
    bool recursive = false;
    int first = 1;
    if (argc > 1 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rf") == 0)) {
        recursive = true;
        first = 2;
    }

    if (first >= argc) {
        writeText("usage: rm [-r] <file>...\n");
        return 1;
    }

    int status = 0;
    for (int i = first; i < argc; i++) {
        char resolved[ShellFs::MAX_PATH];
        if (!resolveArg("rm", argv[i], resolved, sizeof(resolved))) {
            status = 1;
            continue;
        }

        // Refusing to delete a mount point: rm -r /sdcard would otherwise try to empty the whole
        // card, which is never what someone means at a shell prompt.
        if (ShellFs::isRoot(resolved)) {
            writeText("rm: refusing to remove the root\n");
            status = 1;
            continue;
        }

        ShellFs::Result result;
        if (ShellFs::isDirectory(resolved)) {
            result = recursive ? ShellFs::removeTree(resolved) : ShellFs::removeDirectory(resolved);
        } else {
            result = ShellFs::removeFile(resolved);
        }
        status |= reportResult("rm", argv[i], result);
    }
    return status;
}

/**
 * Builds the destination for cp/mv. When the target is an existing directory the source's basename
 * is appended, so `cp file dir/` behaves as expected rather than overwriting the directory entry.
 */
bool buildTarget(const char* sourcePath, const char* targetArg, char* out, size_t outSize) {
    char resolvedTarget[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(targetArg, resolvedTarget, sizeof(resolvedTarget))) {
        return false;
    }

    if (!ShellFs::isDirectory(resolvedTarget)) {
        snprintf(out, outSize, "%s", resolvedTarget);
        return true;
    }

    const char* base = strrchr(sourcePath, '/');
    base = (base != nullptr) ? base + 1 : sourcePath;

    const int written = snprintf(out, outSize, "%s/%s", resolvedTarget, base);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

int cmdCp(int argc, char** argv) {
    if (argc < 3) {
        writeText("usage: cp <src> <dst>\n");
        return 1;
    }

    char source[ShellFs::MAX_PATH];
    if (!resolveArg("cp", argv[1], source, sizeof(source))) {
        return 1;
    }
    if (ShellFs::isDirectory(source)) {
        writeText("cp: directories are not supported\n");
        return 1;
    }

    char target[ShellFs::MAX_PATH];
    if (!buildTarget(source, argv[2], target, sizeof(target))) {
        writeText("cp: path too long\n");
        return 1;
    }

    return reportResult("cp", argv[1], ShellFs::copyFile(source, target, true));
}

int cmdMv(int argc, char** argv) {
    if (argc < 3) {
        writeText("usage: mv <src> <dst>\n");
        return 1;
    }

    char source[ShellFs::MAX_PATH];
    if (!resolveArg("mv", argv[1], source, sizeof(source))) {
        return 1;
    }

    char target[ShellFs::MAX_PATH];
    if (!buildTarget(source, argv[2], target, sizeof(target))) {
        writeText("mv: path too long\n");
        return 1;
    }

    return reportResult("mv", argv[1], ShellFs::moveFile(source, target, true));
}

int cmdTouch(int argc, char** argv) {
    if (argc < 2) {
        writeText("usage: touch <file>...\n");
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        char resolved[ShellFs::MAX_PATH];
        if (!resolveArg("touch", argv[i], resolved, sizeof(resolved))) {
            status = 1;
            continue;
        }
        status |= reportResult("touch", argv[i], ShellFs::touchFile(resolved));
    }
    return status;
}

int cmdFree(int, char**) {
    memory_print_stats();
    return 0;
}

// ---------------------------------------------------------------------------
// Text inspection
// ---------------------------------------------------------------------------

/** Parses an optional `-n <count>` prefix, returning the index of the first non-option argument. */
int parseLineCount(int argc, char** argv, int* outCount) {
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        *outCount = atoi(argv[2]);
        return 3;
    }
    return 1;
}

struct HeadState {
    int remaining;
    bool atLineStart;
};

bool emitHeadChunk(const char* data, size_t length, void* context) {
    auto* state = static_cast<HeadState*>(context);

    for (size_t i = 0; i < length; i++) {
        if (state->remaining <= 0) {
            return false;
        }

        const char c = data[i];
        if (c == '\n') {
            writeText("\n");
            state->remaining--;
            state->atLineStart = true;
        } else {
            const char text[2] = { (c == '\t' || (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F)) ? c : '.', '\0' };
            writeText(text);
            state->atLineStart = false;
        }
    }
    return state->remaining > 0;
}

int cmdHead(int argc, char** argv) {
    int count = 10;
    const int first = parseLineCount(argc, argv, &count);
    if (first >= argc) {
        writeText("usage: head [-n N] <file>\n");
        return 1;
    }

    char resolved[ShellFs::MAX_PATH];
    if (!resolveArg("head", argv[first], resolved, sizeof(resolved))) {
        return 1;
    }

    HeadState state { count, true };
    if (!ShellFs::streamFile(resolved, &state, emitHeadChunk)) {
        Shell::print("head: %s: cannot read\n", argv[first]);
        return 1;
    }
    if (!state.atLineStart) {
        writeText("\n");
    }
    return 0;
}

// tail keeps a ring of the last N lines, so the file is still read in one forward pass rather than
// seeking backwards - which would be far more code for no benefit at these file sizes.
constexpr int TAIL_MAX_LINES = 32;
constexpr size_t TAIL_MAX_LINE_LENGTH = 200;

struct TailState {
    char lines[TAIL_MAX_LINES][TAIL_MAX_LINE_LENGTH];
    int count;
    int next;
    size_t columnn;
};

bool collectTailChunk(const char* data, size_t length, void* context) {
    auto* state = static_cast<TailState*>(context);

    for (size_t i = 0; i < length; i++) {
        const char c = data[i];
        if (c == '\n') {
            state->lines[state->next][state->columnn] = '\0';
            state->next = (state->next + 1) % TAIL_MAX_LINES;
            if (state->count < TAIL_MAX_LINES) {
                state->count++;
            }
            state->columnn = 0;
        } else if (state->columnn < TAIL_MAX_LINE_LENGTH - 1) {
            const bool printable = (c == '\t') ||
                (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F);
            state->lines[state->next][state->columnn++] = printable ? c : '.';
        }
    }
    return true;
}

int cmdTail(int argc, char** argv) {
    int count = 10;
    const int first = parseLineCount(argc, argv, &count);
    if (first >= argc) {
        writeText("usage: tail [-n N] <file>\n");
        return 1;
    }
    if (count > TAIL_MAX_LINES) {
        count = TAIL_MAX_LINES;
    }

    char resolved[ShellFs::MAX_PATH];
    if (!resolveArg("tail", argv[first], resolved, sizeof(resolved))) {
        return 1;
    }

    auto* state = static_cast<TailState*>(calloc(1, sizeof(TailState)));
    if (state == nullptr) {
        writeText("tail: out of memory\n");
        return 1;
    }

    if (!ShellFs::streamFile(resolved, state, collectTailChunk)) {
        Shell::print("tail: %s: cannot read\n", argv[first]);
        free(state);
        return 1;
    }

    // A file not ending in a newline leaves a partial line in the buffer that still counts.
    if (state->columnn > 0) {
        state->lines[state->next][state->columnn] = '\0';
        state->next = (state->next + 1) % TAIL_MAX_LINES;
        if (state->count < TAIL_MAX_LINES) {
            state->count++;
        }
    }

    const int show = (count < state->count) ? count : state->count;
    for (int i = show; i > 0; i--) {
        const int index = (state->next - i + TAIL_MAX_LINES * 2) % TAIL_MAX_LINES;
        Shell::print("%s\n", state->lines[index]);
    }

    free(state);
    return 0;
}

struct CountState {
    size_t lines;
    size_t words;
    size_t bytes;
    bool inWord;
};

bool countChunk(const char* data, size_t length, void* context) {
    auto* state = static_cast<CountState*>(context);
    state->bytes += length;

    for (size_t i = 0; i < length; i++) {
        const char c = data[i];
        if (c == '\n') {
            state->lines++;
        }
        const bool space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (!space && !state->inWord) {
            state->words++;
        }
        state->inWord = !space;
    }
    return true;
}

int cmdWc(int argc, char** argv) {
    // Flags select which counts to print; with none, all three are shown, as in POSIX wc.
    bool wantLines = false;
    bool wantWords = false;
    bool wantBytes = false;
    int first = 1;

    while (first < argc && argv[first][0] == '-' && argv[first][1] != '\0') {
        for (const char* f = argv[first] + 1; *f != '\0'; f++) {
            switch (*f) {
                case 'l': wantLines = true; break;
                case 'w': wantWords = true; break;
                case 'c': wantBytes = true; break;
                default:
                    Shell::print("wc: unknown option -%c\n", *f);
                    return 1;
            }
        }
        first++;
    }
    if (!wantLines && !wantWords && !wantBytes) {
        wantLines = wantWords = wantBytes = true;
    }

    CountState state { 0, 0, 0, false };

    if (first >= argc) {
        // No file given: read stdin, which is what makes `cat x | wc -l` work. Under a pipe this
        // is the staging file; at the prompt it is the terminal.
        char chunk[256];
        while (fgets(chunk, sizeof(chunk), stdin) != nullptr) {
            countChunk(chunk, strlen(chunk), &state);
        }
    } else {
        char resolved[ShellFs::MAX_PATH];
        if (!resolveArg("wc", argv[first], resolved, sizeof(resolved))) {
            return 1;
        }
        if (!ShellFs::streamFile(resolved, &state, countChunk)) {
            Shell::print("wc: %s: cannot read\n", argv[first]);
            return 1;
        }
    }

    // Counts first, space separated, then the filename when one was given - so that `wc -l x`
    // yields a single number that command substitution can use directly.
    bool wroteAny = false;
    if (wantLines) {
        Shell::print("%u", (unsigned)state.lines);
        wroteAny = true;
    }
    if (wantWords) {
        Shell::print(wroteAny ? " %u" : "%u", (unsigned)state.words);
        wroteAny = true;
    }
    if (wantBytes) {
        Shell::print(wroteAny ? " %u" : "%u", (unsigned)state.bytes);
    }
    if (first < argc) {
        Shell::print(" %s", argv[first]);
    }
    writeText("\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Filesystem information
// ---------------------------------------------------------------------------

int cmdDf(int, char**) {
    // Only mount points are listed: ESP-IDF's VFS has no statvfs and Tactility's FileSystem API
    // exposes paths but not capacity, so there is no honest way to report free space here.
    writeText("Mounted filesystems:\n");
    ShellFs::forEachMount(nullptr, printMount);
    return 0;
}

int cmdDu(int argc, char** argv) {
    char resolved[ShellFs::MAX_PATH];
    if (!resolveArg("du", argc > 1 ? argv[1] : "", resolved, sizeof(resolved))) {
        return 1;
    }

    if (ShellFs::isRoot(resolved)) {
        writeText("du: cannot size the root\n");
        return 1;
    }

    const uint64_t bytes = ShellFs::treeSize(resolved);
    Shell::print("%u\t%s\n", (unsigned)bytes, resolved);
    return 0;
}

int cmdDate(int, char**) {
    const time_t now = time(nullptr);
    struct tm parts;
    localtime_r(&now, &parts);

    char text[64];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &parts);
    Shell::print("%s\n", text);
    return 0;
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

// The mixer task is started on first use rather than at shell startup, so an app that never makes
// a sound doesn't pay for the task or hold the codec open.
bool soundReady = false;

bool ensureSound() {
    if (soundReady) {
        return true;
    }
    if (snd_init() != ERROR_NONE) {
        writeText("sound: no audio output available\n");
        return false;
    }
    soundReady = true;
    return true;
}

int cmdBeep(int argc, char** argv) {
    if (!ensureSound()) {
        return 1;
    }

    // beep [frequency] [milliseconds] [wave]
    const float frequency = (argc > 1) ? (float)atoi(argv[1]) : 880.0f;
    const int duration = (argc > 2) ? atoi(argv[2]) : 150;
    const int wave = (argc > 3) ? atoi(argv[3]) : SND_WAVE_SQUARE;

    if (frequency <= 0.0f || duration <= 0) {
        writeText("usage: beep [hz] [ms] [wave 0-4]\n");
        return 1;
    }

    snd_note_on(0, frequency, wave, 200);
    vTaskDelay(pdMS_TO_TICKS(duration));
    snd_note_off(0);
    return 0;
}

int cmdPlay(int argc, char** argv) {
    if (argc < 2) {
        writeText("usage: play <notes>   e.g. play cdefgab\n");
        return 1;
    }
    if (!ensureSound()) {
        return 1;
    }

    // Semitone offsets from C within one octave, indexed by note letter a-g.
    static const int SEMITONES[7] = { 9, 11, 0, 2, 4, 5, 7 };
    constexpr int NOTE_MS = 160;

    for (const char* p = argv[1]; *p != '\0'; p++) {
        const char note = (*p >= 'A' && *p <= 'G') ? (char)(*p + 32) : *p;

        if (note == ' ' || note == '-') {
            vTaskDelay(pdMS_TO_TICKS(NOTE_MS));
            continue;
        }
        if (note < 'a' || note > 'g') {
            continue;
        }

        // A4 = 440 Hz; the exponent places each note relative to it in equal temperament.
        const int semitone = SEMITONES[note - 'a'];
        const float frequency = 440.0f * powf(2.0f, (float)(semitone - 9) / 12.0f);

        snd_note_on(0, frequency, SND_WAVE_SQUARE, 200);
        vTaskDelay(pdMS_TO_TICKS(NOTE_MS));
        snd_note_off(0);
    }
    return 0;
}

int cmdVolume(int argc, char** argv) {
    if (!ensureSound()) {
        return 1;
    }
    if (argc > 1) {
        snd_set_volume(atoi(argv[1]));
    }
    Shell::print("volume: %d%%\n", snd_get_volume());
    return 0;
}

// ---------------------------------------------------------------------------
// Script execution
// ---------------------------------------------------------------------------

/**
 * Runs a script file in the current interpreter state.
 *
 * Positional parameters are set from argv for the duration and restored afterwards, so `$1` inside
 * the script refers to the script's own arguments rather than the shell's.
 */
int runScript(const char* resolvedPath, int argc, char** argv) {
    size_t size = 0;
    char* source = ShellFs::readFile(resolvedPath, &size);
    if (source == nullptr) {
        Shell::print("%s: cannot read\n", resolvedPath);
        return 1;
    }

    const int status = Shell::runScriptSource(source, argc, argv);
    free(source);
    return status;
}

/**
 * Decides whether an argument should be made absolute before a loaded binary sees it.
 *
 * Only two things qualify: a path that already exists, and a path whose parent directory exists
 * (a file the program is about to create, as in `wget url out.txt`). Anything else is passed
 * through untouched.
 *
 * Being strict here matters. An earlier version rewrote anything that merely looked like a
 * filename, which turned `vistep 3` into `vistep /sdcard/3` and `grep hello f` into
 * `grep /sdcard/hello f` - the program then saw a nonsense argument and behaved as though it had
 * been given nothing. Search patterns, numbers and subcommands are all indistinguishable from bare
 * filenames, so existence is the only reliable signal.
 */
bool shouldMakeAbsolute(const char* argument, char* out, size_t outSize) {
    if (argument[0] == '\0' || argument[0] == '-' || argument[0] == '/') {
        return false;
    }

    // Reject anything carrying a character a path would not: ':' from URLs, '*'/'?' from patterns.
    for (const char* p = argument; *p != '\0'; p++) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == ':' || c == '*' || c == '?' || c == ' ' || c == '\t' || c < 0x20) {
            return false;
        }
    }

    char candidate[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(argument, candidate, sizeof(candidate))) {
        return false;
    }

    if (ShellFs::exists(candidate)) {
        snprintf(out, outSize, "%s", candidate);
        return true;
    }

    // Not present: only rewrite when it names a file inside a directory that does exist, and when
    // the argument actually looks like a filename rather than a bare word. Requiring either a '/'
    // or an extension keeps `grep hello` and `vistep 3` from being treated as output paths.
    const bool hasSeparator = strchr(argument, '/') != nullptr;
    const char* dot = strrchr(argument, '.');
    const bool hasExtension = dot != nullptr && dot != argument && dot[1] != '\0';
    if (!hasSeparator && !hasExtension) {
        return false;
    }

    char parent[ShellFs::MAX_PATH];
    snprintf(parent, sizeof(parent), "%s", candidate);
    char* lastSlash = strrchr(parent, '/');
    if (lastSlash == nullptr) {
        return false;
    }
    if (lastSlash == parent) {
        parent[1] = '\0';  // the root itself
    } else {
        *lastSlash = '\0';
    }

    if (!ShellFs::isDirectory(parent)) {
        return false;
    }

    snprintf(out, outSize, "%s", candidate);
    return true;
}

/**
 * Loads and runs an ELF binary as its own app instance (app_execute_for_result_with_streams(),
 * app/execute.h) - its own task, stack and fd table, loaded and relocated against the firmware's
 * symbol table by app-module's own AppLoaderApi for APP_LOCATION_PATH. Its stdio is piped through
 * three AppStreams this app owns and pumps below, the same way a real shell's parent process pipes
 * a child's stdio - that is why plain printf()/read() work in these binaries unmodified.
 */
// How often the pump loop below checks for a new keystroke while a loaded binary runs.
constexpr uint32_t ELF_PUMP_INTERVAL_MS = 50;

int runElf(const char* resolvedPath, int argc, char** argv) {
    // Arguments that look like relative paths are made absolute before the binary sees them.
    //
    // A loaded binary calls fopen() directly, and ESP-IDF has no per-process working directory for
    // that to resolve against - there is no chdir() at all. The shell's own cwd lives in ShellFs
    // and means nothing to libc, so `./grep.elf x test.sh` would have the binary open "test.sh"
    // relative to the filesystem root and fail. Rewriting here is what lets unmodified POSIX
    // programs work with relative paths.
    //
    // An argument is rewritten if it either names something that exists, or looks like a plain
    // filename that a program might be about to create - `wget url out.txt` has to work as well as
    // `grep x in.txt`, and the output file does not exist yet by definition.
    //
    // Arguments containing a character no filename would carry (':' from a URL, '*' from a
    // pattern, whitespace) are left alone, so URLs and search patterns pass through untouched.
    char* rewritten[32];
    char storage[8][ShellFs::MAX_PATH];
    int stored = 0;

    const int passedArgc = (argc < static_cast<int>(sizeof(rewritten) / sizeof(rewritten[0])))
        ? argc
        : static_cast<int>(sizeof(rewritten) / sizeof(rewritten[0]));

    for (int i = 0; i < passedArgc; i++) {
        rewritten[i] = argv[i];

        // argv[0] is the binary itself, already resolved by the caller.
        if (i == 0 || stored >= 8) {
            continue;
        }

        if (shouldMakeAbsolute(argv[i], storage[stored], ShellFs::MAX_PATH)) {
            rewritten[i] = storage[stored];
            stored++;
        }
    }

    // argv[0] is given as the resolved path too, so a binary that reopens itself still works.
    rewritten[0] = const_cast<char*>(resolvedPath);

    // $PWD is exported so a binary can undo the argument rewriting above when it needs the name the
    // user actually typed. `tar cf a.tar test.sh` receives /sdcard/test.sh, and storing that in the
    // archive would extract as sdcard/test.sh under the destination; tar strips this prefix to
    // recover "test.sh". Only tools that record or print paths need it - everything else just opens
    // what it is given. There is no per-process environment here, so this leaks into the shell's
    // own, which is harmless: the shell reads its cwd from ShellFs, never from getenv.
    setenv("PWD", ShellFs::cwd(), 1);

    // Output written by the binary should appear as it is produced rather than sitting in a buffer
    // until the binary happens to exit.
    fflush(stdout);

    // The terminal still holds whatever the last program left behind, so clear it before this one
    // starts drawing - otherwise the first frame shows the previous program's screen underneath.
    // Bypasses any active redirect (fwrite() would honour it) and fwrite()'s own capture gap (see
    // Shell::writeRaw()) - a screen clear belongs to the terminal regardless of where this
    // command's own output is going, the same way LineEditor's prompt/echo does.
    ::write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);

    /*
     * Runs the binary as its own app instance - its own task and stack, loaded and run by
     * app-module's own AppLoaderApi for APP_LOCATION_PATH - rather than in this one's process.
     * Its stdio is piped through three AppStreams: this task feeds stdinStream from the keyboard
     * and drains stdoutStream/stderrStream to the screen below, the same way a real shell's
     * parent process would.
     */
    static uint8_t stdinBuffer[256];
    static uint8_t stdoutBuffer[1024];
    static uint8_t stderrBuffer[512];
    AppStream stdinStream {};
    AppStream stdoutStream {};
    AppStream stderrStream {};

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);

    AppStreamBinding bindings[] = {
        { STDIN_FILENO, &stdinStream, stdinBuffer, sizeof(stdinBuffer), &eventGroup },
        { STDOUT_FILENO, &stdoutStream, stdoutBuffer, sizeof(stdoutBuffer), &eventGroup },
        { STDERR_FILENO, &stderrStream, stderrBuffer, sizeof(stderrBuffer), &eventGroup },
    };

    AppEventSubscription eventSub {};
    app_event_subscribe(&eventSub, &eventGroup);

    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(resolvedPath) };
    AppInstanceId childId = 0;
    error_t result = app_execute_for_result_with_streams(
        location, AppStackConfig {}, passedArgc, rewritten,
        bindings, sizeof(bindings) / sizeof(bindings[0]),
        app_scheduler_current_app_id(), &childId);

    int exitCode;
    if (result != ERROR_NONE) {
        // Covers both "not a valid/runnable ELF" and "references a symbol the firmware does not
        // export"; the loader logs specifics to the serial console.
        Shell::print("%s: failed to load (see serial log for details)\n", argv[0]);
        exitCode = 126;
    } else {
        /*
         * Pumps input to the child and its output back out until it exits.
         *
         * Polls this app's own stdin rather than blocking on it: a plain read() would also block
         * forever once the child stops reading it (a non-interactive tool, or one that exits before
         * draining what was typed ahead), leaving no way to notice the child has exited. A bounded
         * app_io_await() doubles as that periodic check.
         */
        int32_t childResult = 2; // AppResultEventData's "Error", if the loop below somehow ends
                                  // without ever observing the matching APP_EVENT_RESULT
        bool childDone = false;
        bool ownStdinClosed = false;
        uint8_t drain[256];

        while (!childDone) {
            if (!ownStdinClosed &&
                app_io_await(STDIN_FILENO, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(ELF_PUMP_INTERVAL_MS)) == ERROR_NONE) {
                char ch = 0;
                const ssize_t n = app_io_read(STDIN_FILENO, &ch, 1);
                if (n == 1) {
                    app_stream_write(&stdinStream, &ch, 1);
                } else {
                    // The terminal running this shell hung up (touch-to-exit): propagate that to
                    // the child the same way, since it never sees this app's own stdin directly.
                    app_stream_close(&stdinStream);
                    ownStdinClosed = true;
                }
            }

            size_t n;
            while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
                Shell::writeRaw(drain, n);
            }
            while ((n = app_stream_read(&stderrStream, drain, sizeof(drain))) > 0) {
                Shell::writeRaw(drain, n);
            }

            AppEvent event {};
            while (app_event_poll(&eventSub, &event) == ERROR_NONE) {
                if (event.type == APP_EVENT_RESULT && event.result.launch_id == childId) {
                    childResult = event.result.result;
                    childDone = true;
                }
            }
        }

        // The child may have written its last bytes and exited before the loop above's last read
        // saw them.
        size_t n;
        while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
            Shell::writeRaw(drain, n);
        }
        while ((n = app_stream_read(&stderrStream, drain, sizeof(drain))) > 0) {
            Shell::writeRaw(drain, n);
        }

        // Only app_stream_unsubscribe() guarantees the fd-table binding is gone and no AppFileOps
        // call is still in flight, which is what makes these stack-local AppStreams safe to let go
        // out of scope below; app_stream_close() (already implied by the child's own exit) does
        // neither on its own. Must happen before app_manager_stop() reaps the child, in case that
        // races app_fd_table_teardown()'s own close() of these same fds.
        app_stream_unsubscribe(&stdinStream);
        app_stream_unsubscribe(&stdoutStream);
        app_stream_unsubscribe(&stderrStream);

        app_manager_stop(childId);
        exitCode = childResult;
    }

    app_event_unsubscribe(&eventSub);
    task_event_group_destruct(&eventGroup);

    fflush(stdout);
    return exitCode;
}

int cmdSh(int argc, char** argv) {
    if (argc < 2) {
        writeText("usage: sh <script> [args...]\n");
        return 1;
    }

    char resolved[ShellFs::MAX_PATH];
    if (!resolveArg("sh", argv[1], resolved, sizeof(resolved))) {
        return 1;
    }
    if (!ShellFs::exists(resolved)) {
        Shell::print("sh: %s: not found\n", argv[1]);
        return 1;
    }

    // Shift so the script sees itself as $0 and its own arguments as $1..$N.
    return runScript(resolved, argc - 1, argv + 1);
}

void printHelpLine(const Shell::Command& command, void*) {
    Shell::print("  %-10s %s\n", command.name, command.help);
}

int cmdHelp(int, char**) {
    writeText("Commands:\n");
    Shell::forEachCommand(nullptr, printHelpLine);
    return 0;
}

struct WhichSearch {
    const char* name;
    bool found;
};

void matchBuiltin(const Shell::Command& command, void* context) {
    auto* search = static_cast<WhichSearch*>(context);
    if (strcmp(command.name, search->name) == 0) {
        search->found = true;
    }
}

/*
 * Reports where a command comes from: a shell builtin, a bundled binary, or nothing.
 *
 * The lookup order here mirrors what execute() actually does, so `which` answers the question that
 * matters - which one would run - rather than merely listing what exists.
 */
int cmdWhich(int argc, char** argv) {
    if (argc < 2) {
        writeText("Usage: which <command>...\n");
        return 1;
    }

    int missing = 0;
    for (int i = 1; i < argc; i++) {
        WhichSearch search { argv[i], false };
        Shell::forEachCommand(&search, matchBuiltin);
        if (search.found) {
            Shell::print("%s: shell builtin\n", argv[i]);
            continue;
        }

        char path[ShellFs::MAX_PATH];
        if (ShellFs::bundledBinaryPath(argv[i], path, sizeof(path))) {
            Shell::print("%s\n", path);
            continue;
        }

        Shell::print("%s: not found\n", argv[i]);
        missing++;
    }

    return missing == 0 ? 0 : 1;
}

const Shell::Command COMMANDS[] = {
    { "help",  "Show this list",                 cmdHelp },
    { "which", "Locate a command",               cmdWhich },
    { "ls",    "List directory",                 cmdLs },
    { "cat",   "Print file contents",            cmdCat },
    { "head",  "Show first lines [-n N]",        cmdHead },
    { "tail",  "Show last lines [-n N]",         cmdTail },
    { "wc",    "Count lines, words and bytes",   cmdWc },
    { "mkdir", "Create directory [-p]",          cmdMkdir },
    { "rm",    "Remove file or directory [-r]",  cmdRm },
    { "cp",    "Copy file",                      cmdCp },
    { "mv",    "Move or rename file",            cmdMv },
    { "touch", "Create an empty file",           cmdTouch },
    { "df",    "List mounted filesystems",       cmdDf },
    { "du",    "Show size of a file or tree",    cmdDu },
    { "sh",    "Run a script file",              cmdSh },
    { "date",  "Show the current date and time", cmdDate },
    { "printf","Format and print",              cmdPrintf },
    { "clear", "Clear the screen",               cmdClear },
    { "free",  "Show memory usage",              cmdFree },
    { "beep",  "Beep [hz] [ms] [wave 0-8]",      cmdBeep },
    { "play",  "Play notes, e.g. play cdefgab",  cmdPlay },
    { "vol",   "Show or set sound volume",       cmdVolume },
};

constexpr int COMMAND_COUNT = sizeof(COMMANDS) / sizeof(COMMANDS[0]);


} // namespace

namespace Shell {

void init() {
    ShellFs::init();
    sh_state_init(&interpreter);

    // stdout is already piped to the terminal app running this one at this point - installed by
    // app_start_for_result_with_streams() before this app's task began, no redirect active yet.
    terminalStdout = stdout;

    // The interpreter keeps its own idea of the working directory ($PWD, `cd -`), so seed it from
    // ShellFs rather than letting the two disagree from the first command. HOME is what a bare
    // `cd` returns to, and the interpreter treats it as an error when unset.
    sh_set(&interpreter, "PWD", ShellFs::cwd());
    sh_set(&interpreter, "HOME", ShellFs::cwd());
    sh_set(&interpreter, "PS1", "$ ");

    // Where the bundled binaries live. They run by bare name already, but the directory itself is
    // buried under the app's assets path, so $BIN makes it reachable: `ls $BIN`, `cat $BIN/LICENSE.tuilib`.
    char binDir[ShellFs::MAX_PATH];
    if (ShellFs::bundledBinaryDir(binDir, sizeof(binDir))) {
        sh_set(&interpreter, "BIN", binDir);
    }

    // echo/cd/pwd/test/exit are provided by the interpreter itself (sh_builtins.c) and are matched
    // before it ever consults our table - which is correct, since `cd` has to change the shell's
    // own state rather than a child's. Ours were removed to avoid dead code that looks live.
}

void shutdown() {
    sh_state_free(&interpreter);

    if (soundReady) {
        // Silence any held notes first, then hand the codec back so the next app can open it.
        snd_all_off();
        snd_port_deinit();
        soundReady = false;
    }
}

void forEachCommand(void* context, void (*callback)(const Command&, void*)) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        callback(COMMANDS[i], context);
    }
}

void write(const char* text) {
    writeText(text);
}

bool outputIsTerminal() {
    // stdout points at the terminal until sh_redir.c swaps that pointer for a redirect, so
    // comparing against the remembered value is what distinguishes the two.
    return stdout == terminalStdout;
}

void writeRaw(const void* data, size_t length) {
    if (!outputIsTerminal()) {
        // Redirected: has to go through the swapped FILE*, wherever that currently points -
        // fwrite() is the right call here, not a correctness gap (see the terminal branch below).
        fwrite(data, 1, length, stdout);
        return;
    }

    // fwrite() isn't intercepted by this app's stdio wrapping on any platform (see
    // AppStdioWrap.cpp's own comment on why), so it never reaches the AppStream the terminal app
    // captures this app's stdout with - bytes written that way stay invisible on screen. write()
    // is, so call it directly instead.
    const auto* bytes = static_cast<const char*>(data);
    size_t remaining = length;
    while (remaining > 0) {
        // Qualified: unqualified write() here would resolve to Shell::write(const char*) above,
        // not the POSIX syscall.
        const ssize_t written = ::write(STDOUT_FILENO, bytes, remaining);
        if (written <= 0) {
            break;
        }
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }
}

namespace {

/** Accumulates completion candidates: how many matched, and the prefix they all share. */
struct Candidates {
    const char* word;
    size_t wordLength;
    char common[ShellFs::MAX_PATH];
    bool haveCommon;
    int count;
    // Printed lazily, so a unique match completes silently without disturbing the prompt.
    char first[ShellFs::MAX_PATH];
};

void offerCandidate(Candidates& candidates, const char* name) {
    if (strncmp(name, candidates.word, candidates.wordLength) != 0) {
        return;
    }

    candidates.count++;

    if (!candidates.haveCommon) {
        snprintf(candidates.common, sizeof(candidates.common), "%s", name);
        snprintf(candidates.first, sizeof(candidates.first), "%s", name);
        candidates.haveCommon = true;
        return;
    }

    // Shorten the shared prefix to whatever this candidate still agrees with.
    size_t i = 0;
    while (candidates.common[i] != '\0' && name[i] != '\0' && candidates.common[i] == name[i]) {
        i++;
    }
    candidates.common[i] = '\0';
}

void offerCommand(const Shell::Command& command, void* context) {
    offerCandidate(*static_cast<Candidates*>(context), command.name);
}

void offerEntry(const ShellFs::Entry& entry, void* context) {
    offerCandidate(*static_cast<Candidates*>(context), entry.name);
}

void printCandidateCommand(const Shell::Command& command, void* context) {
    auto* candidates = static_cast<Candidates*>(context);
    if (strncmp(command.name, candidates->word, candidates->wordLength) == 0) {
        Shell::print("%s  ", command.name);
    }
}

void printCandidateEntry(const ShellFs::Entry& entry, void* context) {
    auto* candidates = static_cast<Candidates*>(context);
    if (strncmp(entry.name, candidates->word, candidates->wordLength) == 0) {
        Shell::print("%s%s  ", entry.name, entry.isDirectory ? "/" : "");
    }
}

} // namespace

bool complete(const char* line, char* outSuffix, size_t suffixSize, bool* outListed) {
    *outListed = false;
    outSuffix[0] = '\0';

    // Find the word under the cursor, which is everything after the last space.
    const char* wordStart = strrchr(line, ' ');
    const bool isFirstWord = (wordStart == nullptr);
    wordStart = isFirstWord ? line : wordStart + 1;

    Candidates candidates {};
    candidates.word = wordStart;
    candidates.wordLength = strlen(wordStart);

    // Paths complete against a directory, which may be named in the word itself ("ls /data/fo").
    char directory[ShellFs::MAX_PATH] = {};
    const char* namePart = wordStart;

    if (!isFirstWord) {
        const char* slash = strrchr(wordStart, '/');
        if (slash != nullptr) {
            char prefix[ShellFs::MAX_PATH];
            const size_t prefixLength = static_cast<size_t>(slash - wordStart);
            if (prefixLength >= sizeof(prefix)) {
                return false;
            }
            memcpy(prefix, wordStart, prefixLength);
            prefix[prefixLength] = '\0';

            // A leading "/foo" leaves an empty prefix, which means the root itself.
            if (!ShellFs::resolvePath(prefixLength == 0 ? "/" : prefix, directory, sizeof(directory))) {
                return false;
            }
            namePart = slash + 1;
        } else {
            snprintf(directory, sizeof(directory), "%s", ShellFs::cwd());
        }

        candidates.word = namePart;
        candidates.wordLength = strlen(namePart);
    }

    if (isFirstWord) {
        forEachCommand(&candidates, offerCommand);
    } else if (ShellFs::isRoot(directory)) {
        // The synthetic root lists mounts rather than directory entries.
        ShellFs::forEachMount(&candidates, [](const char* path, void* context) {
            const char* name = strrchr(path, '/');
            offerCandidate(*static_cast<Candidates*>(context), (name != nullptr) ? name + 1 : path);
        });
    } else {
        ShellFs::listDirectory(directory, &candidates, offerEntry);
    }

    if (candidates.count == 0) {
        return false;
    }

    // Everything matched shares at least the typed word, so append only what is beyond it.
    const size_t commonLength = strlen(candidates.common);
    if (commonLength > candidates.wordLength) {
        snprintf(outSuffix, suffixSize, "%s", candidates.common + candidates.wordLength);
    }

    // A single match is unambiguous: finish it, and add a separator so the next word can be typed.
    if (candidates.count == 1) {
        const size_t used = strlen(outSuffix);
        if (used + 1 < suffixSize) {
            const bool directoryMatch = !isFirstWord && [&] {
                char full[ShellFs::MAX_PATH];
                const int written = snprintf(full, sizeof(full), "%s/%s", directory, candidates.first);
                return written > 0 && static_cast<size_t>(written) < sizeof(full) && ShellFs::isDirectory(full);
            }();
            outSuffix[used] = directoryMatch ? '/' : ' ';
            outSuffix[used + 1] = '\0';
        }
        return true;
    }

    // Several matches and nothing more to add: show what they are.
    if (outSuffix[0] == '\0') {
        writeText("\n");
        if (isFirstWord) {
            forEachCommand(&candidates, printCandidateCommand);
        } else if (ShellFs::isRoot(directory)) {
            ShellFs::forEachMount(&candidates, [](const char* path, void* context) {
                const char* name = strrchr(path, '/');
                Shell::print("%s  ", (name != nullptr) ? name + 1 : path);
            });
        } else {
            ShellFs::listDirectory(directory, &candidates, printCandidateEntry);
        }
        writeText("\n");
        *outListed = true;
    }

    return true;
}

void print(const char* format, ...) {
    // Formatted into a buffer and sent through writeRaw() rather than vfprintf(stdout, ...):
    // buffered <cstdio> output isn't guaranteed to reach the underlying fd (and so the AppStream
    // capturing it) on every call - see writeRaw()'s own doc. writeRaw() is what makes redirection
    // still work here - see writeText().
    va_list args;
    va_start(args, format);
    va_list argsForStack;
    va_copy(argsForStack, args);
    char stackBuffer[256];
    const int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), format, argsForStack);
    va_end(argsForStack);

    if (needed < 0) {
        va_end(args);
        return;
    }
    if (static_cast<size_t>(needed) < sizeof(stackBuffer)) {
        writeRaw(stackBuffer, static_cast<size_t>(needed));
    } else {
        auto heapBuffer = std::make_unique<char[]>(static_cast<size_t>(needed) + 1);
        vsnprintf(heapBuffer.get(), static_cast<size_t>(needed) + 1, format, args);
        writeRaw(heapBuffer.get(), static_cast<size_t>(needed));
    }
    va_end(args);
}

int runCommand(int argc, char** argv, int* found) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(argv[0], COMMANDS[i].name) == 0) {
            *found = 1;
            return COMMANDS[i].function(argc, argv);
        }
    }

    // Not a builtin: try it as a file. A name containing '/' is taken as a path; a bare name is
    // looked up in the app's bundled binaries, which stands in for a PATH search. A bare name that
    // matches nothing there stays a fast "command not found" rather than a directory scan.
    char resolved[ShellFs::MAX_PATH];
    bool haveFile = false;

    if (strchr(argv[0], '/') != nullptr) {
        haveFile = ShellFs::resolvePath(argv[0], resolved, sizeof(resolved)) &&
            ShellFs::exists(resolved) && !ShellFs::isDirectory(resolved);
    } else {
        haveFile = ShellFs::bundledBinaryPath(argv[0], resolved, sizeof(resolved));
    }

    if (haveFile) {
        *found = 1;
        // ELF binaries and shell scripts are told apart by content rather than by extension,
        // since the filesystem is FAT and carries no execute bit to consult.
        return ShellFs::isElf(resolved)
            ? runElf(resolved, argc, argv)
            : runScript(resolved, argc, argv);
    }

    *found = 0;
    return 127;
}

int runScriptSource(const char* source, int argc, char** argv) {
    // Scripts run on their own interpreter state rather than the session's. A script is reached
    // from inside sh_run_string() - the interpreter calls runCommand() for the `sh foo` line while
    // the outer parse is still in progress - and a fresh state keeps the two runs independent.
    //
    // The consequence is that a script cannot see or modify the session's variables. `.` and
    // `source` remain the way to run something in the current shell, which is what they are for.
    sh_state scriptState;
    sh_state_init(&scriptState);

    // Carry the environment-ish basics across so scripts see a sane world.
    if (const char* home = sh_get(&interpreter, "HOME")) {
        sh_set(&scriptState, "HOME", home);
    }
    if (const char* bin = sh_get(&interpreter, "BIN")) {
        sh_set(&scriptState, "BIN", bin);
    }
    sh_set(&scriptState, "PWD", ShellFs::cwd());

    const int status = sh_run_string_args(&scriptState, source, argc, argv);

    sh_state_free(&scriptState);
    return status;
}

void execute(const char* line) {
    // Everything goes through the shell interpreter, so a bare "ls" and a full
    // `for f in *.txt; do wc -l $f; done` take the same path. It calls back into runCommand()
    // via sh_port_run_external() once a command line has been expanded.
    sh_run_string(&interpreter, line);
}

} // namespace Shell

// ---------------------------------------------------------------------------
// Bridge for the vendored shell core, which is C and cannot see the above.
// ---------------------------------------------------------------------------

extern "C" {

int shell_bridge_run_command(int argc, char** argv, int* found) {
    if (argc <= 0) {
        *found = 0;
        return 127;
    }
    return Shell::runCommand(argc, argv, found);
}

void shell_bridge_temp_path(int which, char* buf, int bufsz) {
    // Pipeline staging files go in the app's own data directory: the working directory could be a
    // read-only mount, and scratch files appearing wherever the user happens to be standing is
    // unfriendly. Falls back to the cwd only if the app directory is unavailable.
    char directory[ShellFs::MAX_PATH];
    size_t size = sizeof(directory);

    if (ShellFs::appDataPath(directory, &size)) {
        snprintf(buf, bufsz, "%s/.sh_pipe_%d", directory, which);
    } else {
        snprintf(buf, bufsz, "%s/.sh_pipe_%d", ShellFs::cwd(), which);
    }
}

int shell_bridge_chdir(const char* path) {
    return ShellFs::changeDirectory(path) ? 0 : -1;
}

void shell_bridge_getcwd(char* buf, int bufsz) {
    snprintf(buf, bufsz, "%s", ShellFs::cwd());
}

int shell_bridge_resolve(const char* path, char* buf, int bufsz) {
    return ShellFs::resolvePath(path, buf, static_cast<size_t>(bufsz)) ? 0 : -1;
}

char* shell_bridge_read_file(const char* path, size_t* outSize) {
    char resolved[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(path, resolved, sizeof(resolved))) {
        return nullptr;
    }
    return ShellFs::readFile(resolved, outSize);
}

} // extern "C"
