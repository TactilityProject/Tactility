#include <Tactility/file/File.h>

#include <cstring>
#include <fstream>
#include <unistd.h>

#include <Tactility/Logger.h>
#include <Tactility/StringUtils.h>

namespace tt::hal::sdcard {
class SdCardDevice;
}

namespace tt::file {

static const auto LOGGER = Logger("file");

class NoLock final : public Lock {
    bool lock(TickType_t timeout) const override { return true; }
    void unlock() const override { /* NO-OP */ }
};

static std::shared_ptr<Lock> noLock = std::make_shared<NoLock>();
static std::function<std::shared_ptr<Lock>(const std::string&)> findLockFunction = nullptr;

std::shared_ptr<Lock> getLock(const std::string& path) {
    if (findLockFunction == nullptr) {
        LOGGER.warn("File lock function not set!");
        return noLock;
    }

    auto lock = findLockFunction(path);
    if (lock == nullptr) {
        return noLock;
    }

    return lock;
}

void setFindLockFunction(const FindLockFunction& function) {
    findLockFunction = function;
}

std::string getChildPath(const std::string& basePath, const std::string& childPath) {
    // Postfix with "/" when the current path isn't "/"
    if (basePath.length() != 1) {
        return basePath + "/" + childPath;
    } else {
        return "/" + childPath;
    }
}

int direntFilterDotEntries(const dirent* entry) {
    return (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0) ? -1 : 0;
}

bool direntSortAlphaAndType(const dirent& left, const dirent& right) {
    bool left_is_dir = left.d_type == TT_DT_DIR || left.d_type == TT_DT_CHR;
    bool right_is_dir = right.d_type == TT_DT_DIR || right.d_type == TT_DT_CHR;
    if (left_is_dir == right_is_dir) {
        return strcmp(left.d_name, right.d_name) < 0;
    } else {
        return left_is_dir > right_is_dir;
    }
}

bool listDirectory(
    const std::string& path,
    std::function<void(const dirent&)> onEntry
) {
    auto lock = getLock(path)->asScopedLock();
    lock.lock();

    LOGGER.info("listDir start {}", path);
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        LOGGER.error("Failed to open dir {}", path);
        return false;
    }

    dirent* current_entry;
    while ((current_entry = readdir(dir)) != nullptr) {
        onEntry(*current_entry);
    }

    closedir(dir);

    LOGGER.info("listDir stop {}", path);
    return true;
}

int scandir(
    const std::string& path,
    std::vector<dirent>& outList,
    ScandirFilter filterMethod,
    ScandirSort sortMethod
) {
    auto lock = getLock(path)->asScopedLock();
    lock.lock();

    LOGGER.info("scandir start");
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        LOGGER.error("Failed to open dir {}", path);
        return -1;
    }

    dirent* current_entry;
    while ((current_entry = readdir(dir)) != nullptr) {
        if (filterMethod == nullptr || filterMethod(current_entry) == 0) {
            outList.push_back(*current_entry);
        }
    }

    closedir(dir);

    if (sortMethod != nullptr) {
        std::ranges::sort(outList, sortMethod);
    }

    LOGGER.info("scandir finish");
    return outList.size();
}


long getSize(FILE* file) {
    long original_offset = ftell(file);

    if (fseek(file, 0, SEEK_END) != 0) {
        LOGGER.error("fseek failed");
        return -1;
    }

    long file_size = ftell(file);
    if (file_size == -1) {
        LOGGER.error("Could not get file length");
        return -1;
    }

    if (fseek(file, original_offset, SEEK_SET) != 0) {
        LOGGER.error("fseek Failed");
        return -1;
    }

    return file_size;
}

/** Read a file.
 * @param[in] filepath
 * @param[out] outSize the amount of bytes that were read, excluding the sizePadding
 * @param[in] sizePadding optional padding to add at the end of the output data (the values are not set)
 */
static std::unique_ptr<uint8_t[]> readBinaryInternal(const std::string& filepath, size_t& outSize, size_t sizePadding = 0) {
    FILE* file = fopen(filepath.c_str(), "rb");

    if (file == nullptr) {
        LOGGER.error("Failed to open {}", filepath);
        return nullptr;
    }

    long content_length = getSize(file);
    if (content_length == -1) {
        LOGGER.error("Failed to determine content length for {}", filepath);
        return nullptr;
    }

    auto data = std::make_unique<uint8_t[]>(content_length + sizePadding);
    if (data == nullptr) {
        LOGGER.error("Insufficient memory. Failed to allocate {} bytes.", content_length);
        return nullptr;
    }

    size_t buffer_offset = 0;
    while (buffer_offset < content_length) {
        size_t bytes_read = fread(&data.get()[buffer_offset], 1, content_length - buffer_offset, file);
        LOGGER.debug("Read {} bytes", bytes_read);
        if (bytes_read > 0) {
            buffer_offset += bytes_read;
        } else { // Something went wrong?
            break;
        }
    }

    fclose(file);

    if (buffer_offset == content_length) {
        outSize = buffer_offset;
        return data;
    } else {
        outSize = 0;
        return nullptr;
    }
}

std::unique_ptr<uint8_t[]> readBinary(const std::string& filepath, size_t& outSize) {
    return readBinaryInternal(filepath, outSize);
}

std::unique_ptr<uint8_t[]> readString(const std::string& filepath) {
    size_t size = 0;
    auto data = readBinaryInternal(filepath, size, 1);
    if (data == nullptr) {
        return nullptr;
    }

    data.get()[size] = 0; // Append null terminator
    return data;
}

bool writeString(const std::string& filepath, const std::string& content) {
    std::ofstream fileStream(filepath);

    if (!fileStream.is_open()) {
        return false;
    }

    fileStream << content;
    fileStream.close();

    return true;
}

static bool findOrCreateDirectoryInternal(std::string path, mode_t mode) {
    auto lock = getLock(path)->asScopedLock();
    lock.lock();

    struct stat dir_stat;
    if (mkdir(path.c_str(), mode) == 0) {
        return true;
    }

    if (errno != EEXIST) {
        return false;
    }

    if (stat(path.c_str(), &dir_stat) != 0) {
        return false;
    }

    if (!S_ISDIR(dir_stat.st_mode)) {
        return false;
    }

    return true;
}

std::string getLastPathSegment(const std::string& path) {
    auto index = path.find_last_of('/');
    if (index != std::string::npos) {
        return path.substr(index + 1);
    } else {
        return path;
    }
}

std::string getFirstPathSegment(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    auto start_index = path[0] == SEPARATOR ? 1 : 0;
    auto index = path.find_first_of(SEPARATOR, start_index);
    if (index != std::string::npos) {
        return path.substr(0, index);
    } else {
        return path;
    }
}

bool findOrCreateDirectory(const std::string& path, mode_t mode) {
    if (path.empty()) {
        return true;
    }
    LOGGER.debug("findOrCreate: {} {}", path, mode);

    const char separator_to_find[] = {SEPARATOR, 0x00};
    auto first_index = path[0] == SEPARATOR ? 1 : 0;
    auto separator_index = path.find(separator_to_find, first_index);

    bool should_break = false;
    while (!should_break) {
        bool is_last_segment = (separator_index == std::string::npos);
        auto to_create = is_last_segment ? path : path.substr(0, separator_index);
        should_break = is_last_segment;
        if (!findOrCreateDirectoryInternal(to_create, mode)) {
            LOGGER.error("Failed to create {}", to_create);
            return false;
        } else {
            LOGGER.debug("  - got: {}", to_create);
        }

        // Find next file separator index
        separator_index = path.find(separator_to_find, separator_index + 1);
    }

    return true;
}

bool findOrCreateParentDirectory(const std::string& path, mode_t mode) {
    std::string parent;
    if (!string::getPathParent(path, parent)) {
        return false;
    }
    return findOrCreateDirectory(parent, mode);
}

bool deleteRecursively(const std::string& path) {
    if (path.empty()) {
        return true;
    }

    if (isDirectory(path)) {
        std::vector<dirent> entries;
        if (scandir(path, entries) < 0) {
            LOGGER.error("Failed to scan directory {}", path);
            return false;
        }

        for (const auto& entry : entries) {
            auto child_path = path + "/" + entry.d_name;
            if (!deleteRecursively(child_path)) {
                return false;
            }
        }
        LOGGER.info("Deleting {}", path);
        return deleteDirectory(path);
    } else if (isFile(path)) {
        LOGGER.info("Deleting {}", path);
        return deleteFile(path);
    } else if (path == "/" || path == "." || path == "..") {
        // No-op
        return true;
    } else {
        LOGGER.error("Failed to delete \"{}\": unknown type", path);
        return false;
    }
}

bool deleteFile(const std::string& path) {
    auto lock = getLock(path)->asScopedLock();
    lock.lock();
    return remove(path.c_str()) == 0;
}

bool deleteDirectory(const std::string& path) {
    auto lock = getLock(path)->asScopedLock();
    lock.lock();
    return rmdir(path.c_str()) == 0;
}

bool isFile(const std::string& path) {
    auto lock = getLock(path)->asScopedLock();
    lock.lock();
    return access(path.c_str(), F_OK) == 0;
}

bool isDirectory(const std::string& path) {
    auto lock = getLock(path)->asScopedLock();
    lock.lock();
    struct stat stat_result;
    return stat(path.c_str(), &stat_result) == 0 && S_ISDIR(stat_result.st_mode);
}

bool readLines(const std::string& filePath, bool stripNewLine, std::function<void(const char* line)> callback) {
    auto lock = getLock(filePath)->asScopedLock();
    lock.lock();

    auto* file = fopen(filePath.c_str(), "r");
    if (file == nullptr) {
        return false;
    }

    char line[1024];

    while (fgets(line, sizeof(line), file) != nullptr) {
        // Strip newline
        if (stripNewLine) {
            size_t line_length = strlen(line);
            if (line_length > 0 && line[line_length - 1] == '\n') {
                line[line_length - 1] = '\0';
            }
        }
        // Publish
        callback(line);
    }

    bool success = feof(file);
    fclose(file);
    return success;
}

}
