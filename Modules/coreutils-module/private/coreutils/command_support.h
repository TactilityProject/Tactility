#pragma once

#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>

#include <cstddef>

/**
 * Helpers shared by more than one coreutil app (see Modules/coreutils-module/source/).
 * Defined in source/CommandSupport.cpp.
 *
 * Path resolution here is a module-local copy of what was Tactility's ShellFs::resolvePath():
 * each coreutil app runs as its own app instance, so a relative argument resolves against this
 * instance's own cwd (app/dir.h), inherited from whichever instance started it - typically the
 * interactive shell, so this agrees with its `pwd`/`cd`. A coreutil has no `cd` of its own: only
 * reads its cwd, never sets it.
 */

/** Resolves an argument, reporting the failure to the terminal. Returns false if it didn't fit. */
bool resolvePathArg(const char* command, const char* path, char* out, size_t outSize);

/** True if a resolved path refers to the synthetic root, which lists mount points rather than files. */
bool isRoot(const char* resolved);

/** Reports an error_t if it isn't ERROR_NONE. Returns the shell exit code. */
int reportResult(const char* command, const char* subject, error_t result);

/** True if this app instance's stdout is a real terminal (best-effort: a window size was set on
 * it), as opposed to a redirect or pipe. Gates binary-unsafe formatting, e.g. substituting
 * non-printable bytes, so it only applies when writing straight to the screen. */
bool stdoutIsTerminal();

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
const char* color(const char* sequence);

/**
 * Builds the destination for cp/mv. When the target is an existing directory the source's basename
 * is appended, so `cp file dir/` behaves as expected rather than overwriting the directory entry.
 */
bool buildTarget(const char* sourcePath, const char* targetArg, char* out, size_t outSize);

/** Parses an optional `-n <count>` prefix, returning the index of the first non-option argument. */
int parseLineCount(int argc, char** argv, int* outCount);

/**
 * Streams a file to a callback in chunks, rather than reading it whole: a coreutil app runs on a
 * fixed stack with a shared heap, and slurping a multi-megabyte file to print it would fail where
 * streaming it does not. Return false from the callback to stop early.
 * @return false if the file could not be opened
 */
bool streamFile(const char* resolvedPath, void* context, bool (*callback)(const char* data, size_t length, void* context));
