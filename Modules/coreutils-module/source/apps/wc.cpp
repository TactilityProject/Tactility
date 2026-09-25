#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <cstdio>
#include <cstring>

namespace {

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

} // namespace

namespace coreutils::wc {

static int32_t main(int argc, char* argv[]) {
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
                    printf("wc: unknown option -%c\n", *f);
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
        char path[FILE_MAX_PATH_STRING_LENGTH];
        if (!resolvePathArg("wc", argv[first], path, sizeof(path))) {
            return 1;
        }
        if (!streamFile(path, &state, countChunk)) {
            printf("wc: %s: cannot read\n", argv[first]);
            return 1;
        }
    }

    // Counts first, space separated, then the filename when one was given, so that `wc -l x`
    // yields a single number that command substitution can use directly.
    bool wroteAny = false;
    if (wantLines) {
        printf("%u", (unsigned)state.lines);
        wroteAny = true;
    }
    if (wantWords) {
        printf(wroteAny ? " %u" : "%u", (unsigned)state.words);
        wroteAny = true;
    }
    if (wantBytes) {
        printf(wroteAny ? " %u" : "%u", (unsigned)state.bytes);
    }
    if (first < argc) {
        printf(" %s", argv[first]);
    }
    printf("\n");
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "wc",
    .name = "wc",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::wc
