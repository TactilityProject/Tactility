#pragma once

#include <cstddef>
#include <cstdint>

#include <tactility/paths.h>

/**
 * Filesystem access for the shell, over Tactility's real mounts.
 *
 * Upstream BreezyBox mounts a LittleFS partition of its own and reaches it through linker-wrapped
 * libc calls (`-Wl,-wrap,fopen`). Neither works here: the shell runs as a relocated ELF where
 * linker wrapping is unreliable, and the point of the port is to browse the device's actual
 * filesystems rather than a private sandbox. So path resolution is explicit instead of wrapped,
 * and every caller goes through the helpers below.
 *
 * One rule the upstream code does not have to care about: there is no real root. There is no
 * filesystem mounted at `/`; there are several independent mounts (`/data`, `/sdcard`). `/` is
 * therefore synthesised as a listing of mount points.
 */
namespace ShellFs {

/** Sets the working directory to the first available mount. */
void init();

/**
 * Writes the app's own writable data directory into `buf`, creating it if needed.
 *
 * Used for scratch files (pipeline staging) that should not land in whatever directory the user
 * happens to be standing in, which may also be a read-only mount.
 *
 * @return false if the path did not fit
 */
bool appDataPath(char* buf, size_t* size);

/** Returns the current working directory. */
const char* cwd();

/**
 * Resolves a user-supplied path against the working directory, collapsing `.` and `..`.
 * @return false if the result would not fit in `out`
 */
bool resolvePath(const char* path, char* out, size_t outSize);

/**
 * Changes the working directory. Accepts the synthetic root.
 * @return false if the path does not exist or is not a directory
 */
bool changeDirectory(const char* path);

} // namespace ShellFs
