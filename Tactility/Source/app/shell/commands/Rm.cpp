#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>
#include <cstring>

int cmdRm(int argc, char** argv) {
    bool recursive = false;
    int first = 1;
    if (argc > 1 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rf") == 0)) {
        recursive = true;
        first = 2;
    }

    if (first >= argc) {
        printf("usage: rm [-r] <file>...\n");
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
            printf("rm: refusing to remove the root\n");
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
