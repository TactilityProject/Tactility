#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>

int cmdTouch(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: touch <file>...\n");
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
