#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

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

int cmdHead(int argc, char** argv) {
    int count = 10;
    const int first = parseLineCount(argc, argv, &count);
    if (first >= argc) {
        printf("usage: head [-n N] <file>\n");
        return 1;
    }

    char resolved[ShellFs::MAX_PATH];
    if (!resolveArg("head", argv[first], resolved, sizeof(resolved))) {
        return 1;
    }

    HeadState state { count, true };
    if (!ShellFs::streamFile(resolved, &state, emitHeadChunk)) {
        printf("head: %s: cannot read\n", argv[first]);
        return 1;
    }
    if (!state.atLineStart) {
        printf("\n");
    }
    return 0;
}
