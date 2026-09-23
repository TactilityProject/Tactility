#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>

int cmdCp(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: cp <src> <dst>\n");
        return 1;
    }

    char source[ShellFs::MAX_PATH];
    if (!resolveArg("cp", argv[1], source, sizeof(source))) {
        return 1;
    }
    if (ShellFs::isDirectory(source)) {
        printf("cp: directories are not supported\n");
        return 1;
    }

    char target[ShellFs::MAX_PATH];
    if (!buildTarget(source, argv[2], target, sizeof(target))) {
        printf("cp: path too long\n");
        return 1;
    }

    return reportResult("cp", argv[1], ShellFs::copyFile(source, target, true));
}
