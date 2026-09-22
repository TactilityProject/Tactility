#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>
#include <cstring>

namespace {

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

} // namespace

/*
 * Reports where a command comes from: a shell builtin, a bundled binary, or nothing.
 *
 * The lookup order here mirrors what execute() actually does, so `which` answers the question that
 * matters (which one would run), rather than merely listing what exists.
 */
int cmdWhich(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: which <command>...\n");
        return 1;
    }

    int missing = 0;
    for (int i = 1; i < argc; i++) {
        WhichSearch search { argv[i], false };
        Shell::forEachCommand(&search, matchBuiltin);
        if (search.found) {
            printf("%s: shell builtin\n", argv[i]);
            continue;
        }

        // TODO: Iterate over installed applications, and/or search across PATH variable entries

        printf("%s: not found\n", argv[i]);
        missing++;
    }

    return missing == 0 ? 0 : 1;
}
