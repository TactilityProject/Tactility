#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/Shell.h>

#include <cstdio>

namespace {

void printHelpLine(const Shell::Command& command, void* /*unused*/) {
    printf("  %-10s %s\n", command.name, command.help);
}

} // namespace

int cmdHelp(int, char**) {
    printf("Commands:\n");
    Shell::forEachCommand(nullptr, printHelpLine);
    return 0;
}
