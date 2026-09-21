#pragma once

#include <cstddef>
#include <cstdint>

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

constexpr size_t MAX_PATH = 256;

/** Sets the working directory to the first available mount. */
void init();

/**
 * Writes the app's own writable data directory into `buf`, creating it if needed.
 *
 * Used for scratch files (pipeline staging) that should not land in whatever directory the user
 * happens to be standing in - which may also be a read-only mount.
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

/** True if the path refers to the synthetic root, which lists mount points rather than files. */
bool isRoot(const char* resolved);

/**
 * Changes the working directory. Accepts the synthetic root.
 * @return false if the path does not exist or is not a directory
 */
bool changeDirectory(const char* path);

/** Calls `callback` with each mount point path. Used to list the synthetic root. */
void forEachMount(void* context, void (*callback)(const char* path, void* context));

/**
 * Reads a whole file into a newly allocated buffer.
 * @param[out] outSize receives the byte count, may be null
 * @return the buffer (caller frees), or null on failure
 */
char* readFile(const char* resolvedPath, size_t* outSize);

/** Directory entry passed to listDirectory's callback. */
struct Entry {
    const char* name;
    bool isDirectory;
    size_t size;
};

/**
 * Lists a directory.
 * @return false if the directory could not be opened
 */
bool listDirectory(const char* resolvedPath, void* context, void (*callback)(const Entry& entry, void* context));

/** True if the path exists and is a directory. */
bool isDirectory(const char* resolvedPath);

/** True if the path exists at all, file or directory. */
bool exists(const char* resolvedPath);

/**
 * True if the file starts with the ELF magic number.
 *
 * Used to tell a loadable binary from a shell script. The filesystem is FAT and has no execute bit,
 * so content is the only thing available to go on.
 */
bool isElf(const char* resolvedPath);

/**
 * Finds a binary bundled in the app's assets, by bare name.
 *
 * This stands in for a PATH search: `grep foo file` finds the bundled grep without the user
 * needing to know where it lives. Both the exact name and name + ".elf" are tried.
 *
 * @return false if no such binary is bundled
 */
bool bundledBinaryPath(const char* name, char* out, size_t outSize);

/**
 * Writes the directory the bundled binaries live in.
 *
 * Exported to the shell as $BIN, so the location is reachable without typing the whole assets path:
 * `ls $BIN`, `cat $BIN/LICENSE.tuilib`. bundledBinaryPath() covers running them; this covers
 * looking at them.
 *
 * @return false if the path could not be determined
 */
bool bundledBinaryDir(char* out, size_t outSize);

/**
 * Streams a file to a callback in chunks.
 *
 * Preferred over readFile() for anything that might be large: the shell runs on a fixed stack with
 * a shared heap, and slurping a multi-megabyte file to print it would fail where streaming it does
 * not. Return false from the callback to stop early.
 *
 * @return false if the file could not be opened
 */
bool streamFile(const char* resolvedPath, void* context, bool (*callback)(const char* data, size_t length, void* context));

/** Result of a write operation, so callers can report why something failed. */
enum class Result {
    Ok,
    NotFound,
    Locked,
    Exists,
    NotEmpty,
    IoError
};

/** Returns a human-readable description of a Result. */
const char* describe(Result result);

/** Creates a directory. Pass createParents to create missing intermediate directories too. */
Result makeDirectory(const char* resolvedPath, bool createParents);

/** Deletes a file. */
Result removeFile(const char* resolvedPath);

/** Deletes an empty directory. */
Result removeDirectory(const char* resolvedPath);

/**
 * Recursively deletes a directory and everything under it.
 *
 * @warning Depth is bounded (see MAX_RECURSION_DEPTH in the implementation): a directory tree
 * deeper than that is left partially deleted rather than overflowing the stack.
 */
Result removeTree(const char* resolvedPath);

/**
 * Copies a file, streaming rather than buffering the whole thing.
 * Fails with Result::Exists rather than silently overwriting unless overwrite is set.
 */
Result copyFile(const char* resolvedSource, const char* resolvedTarget, bool overwrite);

/** Renames or moves a file. Falls back to copy-then-delete when crossing mount points. */
Result moveFile(const char* resolvedSource, const char* resolvedTarget, bool overwrite);

/** Creates an empty file, or updates nothing if it already exists. */
Result touchFile(const char* resolvedPath);

/** Writes text to a file, replacing or appending to any existing content. */
Result writeFile(const char* resolvedPath, const char* data, size_t length, bool append);

/**
 * Total size in bytes of a file, or of everything under a directory.
 *
 * Note there is no free-space equivalent: ESP-IDF's VFS has no statvfs, and Tactility's FileSystem
 * API exposes mount paths but not capacity, so `df` lists mounts rather than reporting usage.
 */
uint64_t treeSize(const char* resolvedPath);

} // namespace ShellFs
