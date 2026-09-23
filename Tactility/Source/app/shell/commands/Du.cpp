#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>

int cmdDu(int argc, char** argv) {
    char resolved[ShellFs::MAX_PATH];
    if (!resolveArg("du", argc > 1 ? argv[1] : "", resolved, sizeof(resolved))) {
        return 1;
    }

    if (ShellFs::isRoot(resolved)) {
        printf("du: cannot size the root\n");
        return 1;
    }

    const uint64_t bytes = ShellFs::treeSize(resolved);
    printf("%u\t%s\n", (unsigned)bytes, resolved);
    return 0;
}
