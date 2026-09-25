#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <cstdio>
#include <cstdlib>

namespace {

// tail keeps a ring of the last N lines, so the file is still read in one forward pass rather than
// seeking backwards, which would be far more code for no benefit at these file sizes.
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

} // namespace

namespace coreutils::tail {

static int32_t main(int argc, char* argv[]) {
    int count = 10;
    const int first = parseLineCount(argc, argv, &count);
    if (first >= argc) {
        printf("usage: tail [-n N] <file>\n");
        return 1;
    }
    if (count > TAIL_MAX_LINES) {
        count = TAIL_MAX_LINES;
    }

    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePathArg("tail", argv[first], path, sizeof(path))) {
        return 1;
    }

    auto* state = static_cast<TailState*>(calloc(1, sizeof(TailState)));
    if (state == nullptr) {
        printf("tail: out of memory\n");
        return 1;
    }

    if (!streamFile(path, state, collectTailChunk)) {
        printf("tail: %s: cannot read\n", argv[first]);
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
        printf("%s\n", state->lines[index]);
    }

    free(state);
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "tail",
    .name = "tail",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::tail
