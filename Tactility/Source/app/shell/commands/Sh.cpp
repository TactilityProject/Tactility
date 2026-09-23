#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>

int cmdSh(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: sh <script> [args...]\n");
        return 1;
    }

    char resolved[ShellFs::MAX_PATH];
    if (!resolveArg("sh", argv[1], resolved, sizeof(resolved))) {
        return 1;
    }
    if (!ShellFs::exists(resolved)) {
        printf("sh: %s: not found\n", argv[1]);
        return 1;
    }

    // Shift so the script sees itself as $0 and its own arguments as $1..$N.
    return runScript(resolved, argc - 1, argv + 1);
}
