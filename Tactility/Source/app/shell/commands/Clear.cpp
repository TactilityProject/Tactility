#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <cstdio>

int cmdClear(int, char**) {
    printf("\x1B[2J\x1B[H");
    return 0;
}
