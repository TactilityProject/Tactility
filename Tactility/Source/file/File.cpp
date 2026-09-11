#include <Tactility/file/File.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <unistd.h>

#include <tactility/log.h>
#include <Tactility/StringUtils.h>

namespace tt::hal::sdcard {
class SdCardDevice;
}

namespace tt::file {

constexpr auto* TAG = "file";

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
    LOG_I(TAG, "listDir start %s", path.c_str());
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        LOG_E(TAG, "Failed to open dir %s", path.c_str());
        return false;
    }

    dirent* current_entry;
    while ((current_entry = readdir(dir)) != nullptr) {
        onEntry(*current_entry);
    }

    closedir(dir);

    LOG_I(TAG, "listDir stop %s", path.c_str());
    return true;
}

int scandir(
    const std::string& path,
    std::vector<dirent>& outList,
    ScandirFilter filterMethod,
    ScandirSort sortMethod
) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        LOG_E(TAG, "Failed to open dir %s", path.c_str());
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

    return outList.size();
}


long getSize(FILE* file) {
    long original_offset = ftell(file);

    if (fseek(file, 0, SEEK_END) != 0) {
        LOG_E(TAG, "fseek failed");
        return -1;
    }

    long file_size = ftell(file);
    if (file_size == -1) {
        LOG_E(TAG, "Could not get file length");
        return -1;
    }

    if (fseek(file, original_offset, SEEK_SET) != 0) {
        LOG_E(TAG, "fseek Failed");
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
        LOG_E(TAG, "Failed to open %s", filepath.c_str());
        return nullptr;
    }

    long content_length = getSize(file);
    if (content_length == -1) {
        LOG_E(TAG, "Failed to determine content length for %s", filepath.c_str());
        return nullptr;
    }

    auto data = std::make_unique<uint8_t[]>(content_length + sizePadding);
    if (data == nullptr) {
        LOG_E(TAG, "Insufficient memory. Failed to allocate %ld bytes.", content_length);
        return nullptr;
    }

    size_t buffer_offset = 0;
    while (buffer_offset < content_length) {
        size_t bytes_read = fread(&data.get()[buffer_offset], 1, content_length - buffer_offset, file);
        LOG_D(TAG, "Read %u bytes", (unsigned)bytes_read);
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

static bool findOrCreateDirectoryInternal(const std::string& path, mode_t mode) {
    // Checked before creating, not after: on the SD card's mount root, the FATFS VFS's mkdir()
    // doesn't fail with EEXIST for an already-mounted root (it returns some other errno), so an
    // mkdir-first/errno-check approach never recognizes it as already there.
    struct stat dir_stat;
    if (stat(path.c_str(), &dir_stat) == 0) {
        return S_ISDIR(dir_stat.st_mode);
    }

    if (mkdir(path.c_str(), mode) == 0) {
        return true;
    }
    return errno == EEXIST && stat(path.c_str(), &dir_stat) == 0 && S_ISDIR(dir_stat.st_mode);
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
    LOG_D(TAG, "findOrCreate: %s %u", path.c_str(), (unsigned)mode);

    const char separator_to_find[] = {SEPARATOR, 0x00};
    auto first_index = path[0] == SEPARATOR ? 1 : 0;
    auto separator_index = path.find(separator_to_find, first_index);

    bool should_break = false;
    while (!should_break) {
        bool is_last_segment = (separator_index == std::string::npos);
        auto to_create = is_last_segment ? path : path.substr(0, separator_index);
        should_break = is_last_segment;
        if (!findOrCreateDirectoryInternal(to_create, mode)) {
            LOG_E(TAG, "Failed to create %s", to_create.c_str());
            return false;
        } else {
            LOG_D(TAG, "  - got: %s", to_create.c_str());
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
    if (path == "/" || path == "." || path == "..") {
        return true;
    }

    if (isDirectory(path)) {
        std::vector<dirent> entries;
        if (scandir(path, entries) < 0) {
            LOG_E(TAG, "Failed to scan directory %s", path.c_str());
            return false;
        }

        for (const auto& entry : entries) {
            if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0) {
                continue;
            }
            auto child_path = path + "/" + entry.d_name;
            if (!deleteRecursively(child_path)) {
                return false;
            }
        }
        LOG_I(TAG, "Deleting %s", path.c_str());
        return deleteDirectory(path);
    } else if (isFile(path)) {
        LOG_I(TAG, "Deleting %s", path.c_str());
        return deleteFile(path);
    } else {
        LOG_E(TAG, "Failed to delete \"%s\": unknown type", path.c_str());
        return false;
    }
}

bool deleteFile(const std::string& path) {
    return remove(path.c_str()) == 0;
}

bool deleteDirectory(const std::string& path) {
    return rmdir(path.c_str()) == 0;
}

bool isFile(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

bool isDirectory(const std::string& path) {
    struct stat stat_result;
    return stat(path.c_str(), &stat_result) == 0 && S_ISDIR(stat_result.st_mode);
}

bool readLines(const std::string& filePath, bool stripNewLine, std::function<void(const char* line)> callback) {
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
