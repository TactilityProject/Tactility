#pragma once

#include <stddef.h>

/**
 * C-callable seam between the vendored shell core and the C++ shell.
 *
 * sh_port_tactility.c is plain C (it is compiled as part of the vendored `shell/` tree), while
 * Shell and ShellFs are C++. These functions are the only calls that cross that boundary.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Runs a builtin by name.
 * @param[out] found set to 1 if a command of that name exists, 0 otherwise
 * @return the command's exit status
 */
int shell_bridge_run_command(int argc, char** argv, int* found);

/** Writes a scratch file path for pipeline staging. `which` (0/1) selects between two names. */
void shell_bridge_temp_path(int which, char* buf, int bufsz);

/** Changes the working directory. Returns 0 on success. */
int shell_bridge_chdir(const char* path);

/** Writes the current working directory into `buf`. */
void shell_bridge_getcwd(char* buf, int bufsz);

/**
 * Resolves a user-supplied path against the shell's working directory.
 *
 * The shell tracks its own cwd in ShellFs; the C library has a separate one that never changes, so
 * a relative path handed straight to fopen() resolves against the wrong place. Anything in the
 * vendored core that opens a file by name must go through this first.
 *
 * @return 0 on success
 */
int shell_bridge_resolve(const char* path, char* buf, int bufsz);

/**
 * Reads a whole file into a malloc'd buffer, resolving the path and taking the mount lock.
 *
 * @param[out] outSize receives the byte count, may be NULL
 * @return the buffer (caller frees), or NULL if it could not be read
 */
char* shell_bridge_read_file(const char* path, size_t* outSize);

#ifdef __cplusplus
}
#endif
