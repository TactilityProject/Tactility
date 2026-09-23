#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>
#include <cstring>

int cmdMkdir(int argc, char** argv) {
    bool createParents = false;
    int first = 1;
    if (argc > 1 && strcmp(argv[1], "-p") == 0) {
        createParents = true;
        first = 2;
    }

    if (first >= argc) {
        printf("usage: mkdir [-p] <dir>...\n");
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
