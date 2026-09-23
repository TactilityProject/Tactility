#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <tactility/filesystem/file_system.h>

#include <cstdio>

int cmdLs(int argc, char** argv) {
    char resolved[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(argc > 1 ? argv[1] : "", resolved, sizeof(resolved))) {
        printf("ls: path too long\n");
        return 1;
    }

    // There is no filesystem at "/": it is synthesised from the list of mount points.
    if (ShellFs::isRoot(resolved)) {
        file_system_for_each_mounted(nullptr, printMount);
        return 0;
    }

    if (!ShellFs::listDirectory(resolved, nullptr, printEntry)) {
        printf("ls: %s: cannot read\n", resolved);
        return 1;
    }
    return 0;
}
