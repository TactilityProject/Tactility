#ifdef ESP_PLATFORM

#include <Tactility/service/webserver/AssetVersion.h>

#include <Tactility/file/File.h>

#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <esp_random.h>

#include <tactility/log.h>

namespace tt::service::webserver {

constexpr auto* TAG = "AssetVersion";
constexpr auto* DATA_VERSION_FILE = "/system/app/WebServer/version.json";
constexpr auto* SD_VERSION_FILE = "/sdcard/tactility/webserver/version.json";
constexpr auto* DATA_ASSETS_DIR = "/system/app/WebServer";
constexpr auto* SD_ASSETS_DIR = "/sdcard/tactility/webserver";

static bool loadVersionFromFile(const char* path, AssetVersion& version) {
    if (!file::isFile(path)) {
        LOG_W(TAG, "Version file not found: %s", path);
        return false;
    }

    // Read file content
    std::string content;
    {
        auto lock = file::getLock(path);
        lock->lock(portMAX_DELAY);

        FILE* fp = fopen(path, "r");
        if (!fp) {
            LOG_E(TAG, "Failed to open version file: %s", path);
            lock->unlock();
            return false;
        }

        char buffer[256];
        size_t bytesRead = fread(buffer, 1, sizeof(buffer) - 1, fp);
        bool readError = ferror(fp) != 0;
        fclose(fp);
        lock->unlock();

        if (readError) {
            LOG_E(TAG, "Error reading version file: %s", path);
            return false;
        }
        if (bytesRead == 0) {
            LOG_E(TAG, "Version file is empty: %s", path);
            return false;
        }
        buffer[bytesRead] = '\0';
        content = buffer;
    }

    // Parse JSON
    cJSON* json = cJSON_Parse(content.c_str());
    if (json == nullptr) {
        LOG_E(TAG, "Failed to parse version JSON: %s", path);
        return false;
    }

    cJSON* versionItem = cJSON_GetObjectItem(json, "version");
    if (versionItem == nullptr || !cJSON_IsNumber(versionItem)) {
        LOG_E(TAG, "Invalid version JSON format: %s", path);
        cJSON_Delete(json);
        return false;
    }

    double versionValue = versionItem->valuedouble;
    if (versionValue < 0 || versionValue > UINT32_MAX) {
        LOG_E(TAG, "Version out of valid range [0, %u]: %s", (unsigned)UINT32_MAX, path);
        cJSON_Delete(json);
        return false;
    }
    version.version = static_cast<uint32_t>(versionValue);
    cJSON_Delete(json);

    LOG_I(TAG, "Loaded version %u from %s", (unsigned)version.version, path);
    return true;
}

static bool saveVersionToFile(const char* path, const AssetVersion& version) {
    // Create directory if it doesn't exist
    std::string dirPath(path);
    size_t lastSlash = dirPath.find_last_of('/');
    if (lastSlash != std::string::npos) {
        dirPath = dirPath.substr(0, lastSlash);
        if (!file::isDirectory(dirPath.c_str())) {
            if (!file::findOrCreateDirectory(dirPath.c_str(), 0755)) {
                LOG_E(TAG, "Failed to create directory: %s", dirPath.c_str());
                return false;
            }
        }
    }

    // Create JSON
    cJSON* json = cJSON_CreateObject();
    if (json == nullptr) {
        LOG_E(TAG, "Failed to create JSON object for version");
        return false;
    }
    cJSON_AddNumberToObject(json, "version", version.version);

    char* jsonString = cJSON_Print(json);
    if (jsonString == nullptr) {
        LOG_E(TAG, "Failed to serialize version JSON");
        cJSON_Delete(json);
        return false;
    }
    
    // Write to file
    bool success = false;
    {
        auto lock = file::getLock(path);
        lock->lock(portMAX_DELAY);
        
        FILE* fp = fopen(path, "w");
        if (fp) {
            size_t len = strlen(jsonString);
            size_t written = fwrite(jsonString, 1, len, fp);
            success = (written == len);
            if (success) {
                if (fflush(fp) != 0) {
                    LOG_E(TAG, "Failed to flush version file: %s", path);
                    success = false;
                } else {
                    int fd = fileno(fp);
                    if (fd >= 0 && fsync(fd) != 0) {
                        LOG_E(TAG, "Failed to fsync version file: %s", path);
                        success = false;
                    }
                }
            }
            fclose(fp);
        }
        lock->unlock();
    }
    
    cJSON_free(jsonString);
    cJSON_Delete(json);
    
    if (success) {
        LOG_I(TAG, "Saved version %u to %s", (unsigned)version.version, path);
    } else {
        LOG_E(TAG, "Failed to write version file: %s", path);
    }
    
    return success;
}

bool loadDataVersion(AssetVersion& version) {
    return loadVersionFromFile(DATA_VERSION_FILE, version);
}

bool loadSdVersion(AssetVersion& version) {
    return loadVersionFromFile(SD_VERSION_FILE, version);
}

bool saveDataVersion(const AssetVersion& version) {
    return saveVersionToFile(DATA_VERSION_FILE, version);
}

bool saveSdVersion(const AssetVersion& version) {
    return saveVersionToFile(SD_VERSION_FILE, version);
}

bool hasDataAssets() {
    return file::isDirectory(DATA_ASSETS_DIR);
}

bool hasSdAssets() {
    return file::isDirectory(SD_ASSETS_DIR);
}

static bool copyDirectory(const char* src, const char* dst, int depth = 0) {
    constexpr int MAX_DEPTH = 16;
    if (depth >= MAX_DEPTH) {
        LOG_E(TAG, "Max directory depth exceeded: %s", src);
        return false;
    }
    LOG_I(TAG, "Copying directory: %s -> %s", src, dst);

    // Create destination directory
    if (!file::isDirectory(dst)) {
        if (!file::findOrCreateDirectory(dst, 0755)) {
            LOG_E(TAG, "Failed to create destination directory: %s", dst);
            return false;
        }
    }
    
    // List source directory and copy each entry
    bool copySuccess = true;
    bool listSuccess = file::listDirectory(src, [&](const dirent& entry) {
        // Skip "." and ".." entries (though listDirectory should already filter these)
        if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0) {
            return;
        }
        
        std::string srcPath = file::getChildPath(src, entry.d_name);
        std::string dstPath = file::getChildPath(dst, entry.d_name);

        // Determine entry type - use stat() directly for unknown/unexpected d_type values
        // (FAT/SD card filesystems often return non-standard d_type values)
        // Note: We use stat() directly here instead of file::isDirectory/isFile to avoid
        // deadlock, since listDirectory already holds a lock on the parent directory.
        bool isDir = (entry.d_type == file::TT_DT_DIR);
        bool isReg = (entry.d_type == file::TT_DT_REG);
        if (!isDir && !isReg) {
            struct stat st;
            if (stat(srcPath.c_str(), &st) == 0) {
                isDir = S_ISDIR(st.st_mode);
                isReg = S_ISREG(st.st_mode);
            } else {
                LOG_W(TAG, "Failed to stat entry, skipping: %s", srcPath.c_str());
                return;
            }
        }

        if (isDir) {
            // Recursively copy subdirectory
            if (!copyDirectory(srcPath.c_str(), dstPath.c_str(), depth + 1)) {
                copySuccess = false;
            }
        } else if (isReg) {
            // Copy file - no additional locking needed since listDirectory already holds a lock
            // and we're the only accessor during sync
            std::string tempPath = std::format("{}.tmp.{}", dstPath, esp_random());

            FILE* srcFile = fopen(srcPath.c_str(), "rb");
            if (!srcFile) {
                LOG_E(TAG, "Failed to open source file: %s", srcPath.c_str());
                copySuccess = false;
                return;
            }

            FILE* tempFile = fopen(tempPath.c_str(), "wb");
            if (!tempFile) {
                LOG_E(TAG, "Failed to create temp file: %s", tempPath.c_str());
                fclose(srcFile);
                copySuccess = false;
                return;
            }

            // Copy in chunks (heap-allocated buffer to avoid stack overflow)
            constexpr size_t COPY_BUF_SIZE = 4096;
            auto buffer = std::make_unique<char[]>(COPY_BUF_SIZE);
            size_t bytesRead;
            bool fileCopySuccess = true;
            while ((bytesRead = fread(buffer.get(), 1, COPY_BUF_SIZE, srcFile)) > 0) {
                size_t bytesWritten = fwrite(buffer.get(), 1, bytesRead, tempFile);
                if (bytesWritten != bytesRead) {
                    LOG_E(TAG, "Failed to write to temp file: %s", tempPath.c_str());
                    fileCopySuccess = false;
                    copySuccess = false;
                    break;
                }
            }

            if (fileCopySuccess && ferror(srcFile)) {
                LOG_E(TAG, "Error reading source file: %s", srcPath.c_str());
                fileCopySuccess = false;
                copySuccess = false;
            }

            fclose(srcFile);

            // Flush and sync temp file before closing
            if (fileCopySuccess) {
                if (fflush(tempFile) != 0) {
                    LOG_E(TAG, "Failed to flush temp file: %s", tempPath.c_str());
                    fileCopySuccess = false;
                    copySuccess = false;
                } else {
                    int fd = fileno(tempFile);
                    if (fd >= 0 && fsync(fd) != 0) {
                        LOG_E(TAG, "Failed to fsync temp file: %s", tempPath.c_str());
                        fileCopySuccess = false;
                        copySuccess = false;
                    }
                }
            }

            fclose(tempFile);

            if (fileCopySuccess) {
                // Remove destination if it exists (rename may not overwrite on some filesystems)
                remove(dstPath.c_str());
                // Rename temp file to destination
                if (rename(tempPath.c_str(), dstPath.c_str()) != 0) {
                    LOG_E(TAG, "Failed to rename temp file %s to %s", tempPath.c_str(), dstPath.c_str());
                    remove(tempPath.c_str());
                    fileCopySuccess = false;
                    copySuccess = false;
                }
            } else {
                // Clean up temp file on failure
                remove(tempPath.c_str());
            }

            if (fileCopySuccess) {
                LOG_I(TAG, "Copied file: %s", entry.d_name);
            }
        }
    });

    if (!listSuccess) {
        LOG_E(TAG, "Failed to list source directory: %s", src);
        return false;
    }
    
    return copySuccess;
}

bool syncAssets() {
    LOG_I(TAG, "Starting asset synchronization...");

    // Check if Data partition and SD card exist
    bool dataExists = hasDataAssets();
    bool sdExists = hasSdAssets();

    // FIRST BOOT SCENARIO: Data has version 0, SD card is missing
    if (dataExists && !sdExists) {
        LOG_I(TAG, "First boot - Data exists but SD card backup missing");
        LOG_W(TAG, "Skipping SD backup during boot - will be created on first settings save");
        LOG_W(TAG, "This avoids watchdog timeout if SD card is slow or corrupted");
        return true;  // Don't block boot - defer copy to runtime
    }

    // NO SD CARD: Just ensure Data has default structure
    if (!sdExists) {
        LOG_W(TAG, "No SD card available - creating default Data structure if needed");
        if (!dataExists) {
            if (!file::findOrCreateDirectory(DATA_ASSETS_DIR, 0755)) {
                LOG_E(TAG, "Failed to create Data assets directory");
                return false;
            }
            AssetVersion defaultVersion(0);  // Start at version 0 - SD card updates will be version 1+
            if (!saveDataVersion(defaultVersion)) {
                LOG_E(TAG, "Failed to save default Data version");
                return false;
            }
            LOG_I(TAG, "Created default Data assets structure (version 0)");
        }
        return true;
    }

    // NORMAL OPERATION: Both exist - compare versions
    AssetVersion dataVersion, sdVersion;
    bool hasDataVer = loadDataVersion(dataVersion);
    bool hasSdVer = loadSdVersion(sdVersion);

    if (!hasDataVer) {
        LOG_W(TAG, "No Data version.json - assuming version 0");
        dataVersion.version = 0;
        if (!saveDataVersion(dataVersion)) {
            LOG_W(TAG, "Failed to save default Data version (non-fatal)");
        }
    }

    if (!hasSdVer) {
        LOG_W(TAG, "No SD version.json - assuming version 0");
        sdVersion.version = 0;
        // DON'T save to SD during boot - defer to runtime
        LOG_W(TAG, "Skipping SD version.json creation during boot - will be created on first settings save");
    }

    LOG_I(TAG, "Version comparison - Data: %u, SD: %u", (unsigned)dataVersion.version, (unsigned)sdVersion.version);

    if (sdVersion.version > dataVersion.version) {
        // Firmware update - copy SD -> Data
        LOG_I(TAG, "SD card newer (v%u > v%u) - copying assets SD -> Data (firmware update)",
                 (unsigned)sdVersion.version, (unsigned)dataVersion.version);
        if (!copyDirectory(SD_ASSETS_DIR, DATA_ASSETS_DIR)) {
            LOG_E(TAG, "Failed to copy assets from SD to Data");
            return false;
        }
        LOG_I(TAG, "Firmware update complete - assets updated from SD card");
    } else if (dataVersion.version > sdVersion.version) {
        // User customization - backup Data -> SD
        LOG_W(TAG, "Data newer (v%u > v%u) - deferring SD backup to avoid boot watchdog",
                 (unsigned)dataVersion.version, (unsigned)sdVersion.version);
        LOG_W(TAG, "SD backup will occur on first WebServer settings save");
        return true;  // Don't block boot - defer copy to runtime
    } else {
        LOG_I(TAG, "Versions match (v%u) - no sync needed", (unsigned)dataVersion.version);
    }

    return true;
}

} // namespace

#endif
