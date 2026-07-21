/**
 * All functions in this file can be safely called without manually applying file locks.
 * For calls to C stdlib APIs such as fopen(), always call file::getLock(path) first!
 */
#pragma once

#include <Tactility/TactilityCore.h>
#include <Tactility/Lock.h>

#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

/**
 * @warning SD card access requires a locking mechanism:
 * @warning When using this in the Tactility main project, use `file::getLock()` or `file::withLock()`
 */
namespace tt::file {

/** File types for `dirent`'s `d_type`. */
enum {
    TT_DT_UNKNOWN = 0, // Unknown type
    TT_DT_FIFO = 1, // Named pipe or FIFO
    TT_DT_CHR = 2, // Character device
    TT_DT_DIR = 4, // Directory
    TT_DT_BLK = 6, // Block device
    TT_DT_REG = 8, // Regular file
    TT_DT_LNK = 10, // Symbolic link
    TT_DT_SOCK = 12, // Local-domain socket
    TT_DT_WHT = 14 // Whiteout inodes
};

#ifdef _WIN32
constexpr char SEPARATOR = '\\';
#else
constexpr char SEPARATOR = '/';
#endif

struct FileCloser {
    void operator()(FILE* file) {
        if (file != nullptr) {
            fclose(file);
        }
    }
};

typedef std::function<std::shared_ptr<Lock>(const std::string&)> FindLockFunction;

/**
 * @param[in] path the path to get a lock for
 * @return a lock instance (never null)
 */
std::shared_ptr<Lock> getLock(const std::string& path);

void setFindLockFunction(const FindLockFunction& function);

long getSize(FILE* file);

/** Read a file and return its data.
 * @param[in] filepath the path of the file
 * @param[out] size the amount of bytes that were read
 * @return null on error, or an array of bytes in case of success
 */
std::unique_ptr<uint8_t[]> readBinary(const std::string& filepath, size_t& outSize);

/** Read a file and return a null-terminated string that represents its content.
 * @param[in] filepath the path of the file
 * @return null on error, or an array of bytes in case of success. Empty string returns as a single 0 character.
 */
std::unique_ptr<uint8_t[]> readString(const std::string& filepath);

/** Write text to a file
 * @param[in] path file path to write to
 * @param[in] content file content to write
 * @return true when operation is successful
 */
bool writeString(const std::string& filepath, const std::string& content);

/** Ensure a directory path exists.
 * @param[in] path the directory path to find, or to create recursively
 * @param[in] mode the mode to use when creating directories
 * @return true when the specified path was found, or otherwise creates the directories recursively with the specified mode.
 */
bool findOrCreateDirectory(const std::string& path, mode_t mode);

bool findOrCreateParentDirectory(const std::string& path, mode_t mode);

bool deleteRecursively(const std::string& path);

bool deleteFile(const std::string& path);

bool deleteDirectory(const std::string& path);

/**
 * Concatenate a child path with a parent path, ensuring proper slash inbetween
 * @param basePath an absolute path with or without trailing "/"
 * @param childPath the name of the child path (e.g. subfolder or file)
 * @return the concatenated path
 */
std::string getChildPath(const std::string& basePath, const std::string& childPath);

/**
 * Find the last part of the path. This can either be the filename or the deepest folder name.
 * @param path an absolute or relative path
 * @return the last segment of the specified path
 */
std::string getLastPathSegment(const std::string& path);

/**
 * Find the first part of the path. This is either the root folder, or a file if the latter has no parent folder.
 * @param path an absolute or relative path
 * @return the first segment of the specified path
 */
std::string getFirstPathSegment(const std::string& path);

typedef int (*ScandirFilter)(const dirent*);

typedef bool (*ScandirSort)(const dirent&, const dirent&);

/** Used for sorting by alphanumeric value and file type */
bool direntSortAlphaAndType(const dirent& left, const dirent& right);

/** A filter for filtering out "." and ".." */
int direntFilterDotEntries(const dirent* entry);

bool isFile(const std::string& path);

bool isDirectory(const std::string& path);

/**
 * A scandir()-like implementation that works on ESP32.
 * It does not return "." and ".." items but otherwise functions the same.
 * It returns an allocated output array with allocated dirent instances.
 * The caller is responsible for free-ing the memory of these.
 *
 * @param[in] path path the scan for files and directories
 * @param[out] onEntry a pointer to a function that accepts an entry
 * @return true if the directory exists and listing its contents was successful
 */
bool listDirectory(
    const std::string& path,
    std::function<void(const dirent&)> onEntry
);

/**
 * A scandir()-like implementation that works on ESP32.
 * It does not return "." and ".." items but otherwise functions the same.
 * It returns an allocated output array with allocated dirent instances.
 * The caller is responsible for free-ing the memory of these.
 *
 * @param[in] path path the scan for files and directories
 * @param[out] outList a pointer to vector of dirent
 * @param[in] filter an optional filter to filter out specific items (nullable)
 * @param[in] sort an optional sorting function (nullable)
 * @return the amount of items that were stored in "output" or -1 when an error occurred
 */
int scandir(
    const std::string& path,
    std::vector<dirent>& outList,
    ScandirFilter filter = nullptr,
    ScandirSort sort = nullptr
);

bool readLines(const std::string& filePath, bool stripNewLine, std::function<void(const char* line)> callback);

}
