#include <coreutils/command_support.h>

#include <app/manifest.h>

#include <cstdio>

namespace {

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
            printf("\n");
            state->remaining--;
            state->atLineStart = true;
        } else {
            const char text[2] = { (c == '\t' || (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F)) ? c : '.', '\0' };
            printf("%s", text);
            state->atLineStart = false;
        }
    }
    return state->remaining > 0;
}

} // namespace

namespace coreutils::head {

static int32_t main(int argc, char* argv[]) {
    int count = 10;
    const int first = parseLineCount(argc, argv, &count);
    if (first >= argc) {
        printf("usage: head [-n N] <file>\n");
        return 1;
    }

    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!resolvePathArg("head", argv[first], path, sizeof(path))) {
        return 1;
    }

    HeadState state { count, true };
    if (!streamFile(path, &state, emitHeadChunk)) {
        printf("head: %s: cannot read\n", argv[first]);
        return 1;
    }
    if (!state.atLineStart) {
        printf("\n");
    }
    return 0;
}

extern const ::AppManifest manifest = {
    .id = "head",
    .name = "head",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::head
