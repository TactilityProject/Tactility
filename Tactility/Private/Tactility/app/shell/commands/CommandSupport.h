#pragma once

#include <Tactility/app/shell/ShellFs.h>

#include <cstddef>

struct FileSystem;

/**
 * Helpers shared by more than one command (see Tactility/Source/app/shell/commands/), or used by
 * Shell.cpp's own core (runCommand() calls runScript()/runElf(); shutdown() calls shutdownSound()).
 * Defined in commands/CommandSupport.cpp.
 */

/** Resolves an argument, reporting the failure to the terminal. Returns false if it didn't fit. */
bool resolveArg(const char* command, const char* path, char* out, size_t outSize);

/** Reports a ShellFs::Result if it isn't Ok. Returns the shell exit code. */
int reportResult(const char* command, const char* subject, ShellFs::Result result);

/*
 * Colour, as SGR escape sequences.
 *
 * Only ever emitted when the output is the terminal: a redirect or a pipe must receive plain text,
 * or `ls > files.txt` writes escape sequences into the file and `ls | grep x` matches against them.
 */
extern const char* const COLOUR_RESET;
extern const char* const COLOUR_DIR;
extern const char* const COLOUR_EXEC;
extern const char* const COLOUR_SIZE;
extern const char* const COLOUR_ERROR;

/** Returns the escape sequence, or an empty string when output is not the terminal. */
const char* colour(const char* sequence);

/** True if the name looks like something that can be run. */
bool looksExecutable(const char* name);

/** ls/df: file_system_for_each_mounted() callback that prints one mounted filesystem's path. */
bool printMount(struct FileSystem* fs, void*);

/** ls: prints one directory entry. */
void printEntry(const ShellFs::Entry& entry, void*);

/**
 * Builds the destination for cp/mv. When the target is an existing directory the source's basename
 * is appended, so `cp file dir/` behaves as expected rather than overwriting the directory entry.
 */
bool buildTarget(const char* sourcePath, const char* targetArg, char* out, size_t outSize);

/** Parses an optional `-n <count>` prefix, returning the index of the first non-option argument. */
int parseLineCount(int argc, char** argv, int* outCount);

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
