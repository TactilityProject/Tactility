#pragma once

/**
 * Runs a script file in the current interpreter state.
 *
 * Positional parameters are set from argv for the duration and restored afterwards, so `$1` inside
 * the script refers to the script's own arguments rather than the shell's.
 */
int runScript(const char* resolvedPath, int argc, char** argv);

/**
 * Loads and runs an ELF binary as its own app instance (app_execute_for_result_with_streams(),
 * app/execute.h): its own task, stack and fd table, loaded and relocated against the firmware's
 * symbol table by app-module's own AppLoaderApi for APP_LOCATION_PATH. Its stdio is piped through
 * three AppStreams this app owns and pumps, the same way a real shell's parent process pipes a
 * child's stdio; that is why plain printf()/read() work in these binaries unmodified.
 */
int runElf(const char* resolvedPath, int argc, char** argv);

/**
 * Runs a registered, in-memory app (APP_LOCATION_MEMORY) as its own app instance, the same way
 * runElf() runs a loaded binary as one: its own task and fd table, stdio piped through this app's
 * own. Used for shell commands resolved via Shell::forEachCommand() instead of calling their
 * AppMainFn inline on the shell's own task.
 */
int runFromMemory(const char* id, int argc, char** argv);

void endLineIfNeeded();
