#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>

int cmdMv(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: mv <src> <dst>\n");
        return 1;
    }

    char source[ShellFs::MAX_PATH];
    if (!resolveArg("mv", argv[1], source, sizeof(source))) {
        return 1;
    }

    char target[ShellFs::MAX_PATH];
    if (!buildTarget(source, argv[2], target, sizeof(target))) {
        printf("mv: path too long\n");
        return 1;
    }

    return reportResult("mv", argv[1], ShellFs::moveFile(source, target, true));
}
