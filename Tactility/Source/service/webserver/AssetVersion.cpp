#ifdef ESP_PLATFORM

#include <Tactility/service/webserver/AssetVersion.h>

#include <Tactility/file/File.h>
#include <Tactility/Logger.h>

#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <esp_random.h>

namespace tt::service::webserver {

static const auto LOGGER = tt::Logger("AssetVersion");
constexpr auto* DATA_VERSION_FILE = "/data/webserver/version.json";
constexpr auto* SD_VERSION_FILE = "/sdcard/.tactility/webserver/version.json";
constexpr auto* DATA_ASSETS_DIR = "/data/webserver";
constexpr auto* SD_ASSETS_DIR = "/sdcard/.tactility/webserver";

static bool loadVersionFromFile(const char* path, AssetVersion& version) {
    if (!file::isFile(path)) {
        LOGGER.warn("Version file not found: {}", path);
        return false;
    }
    
    // Read file content
    std::string content;
    {
        auto lock = file::getLock(path);
        lock->lock(portMAX_DELAY);
        
        FILE* fp = fopen(path, "r");
        if (!fp) {
            LOGGER.error("Failed to open version file: {}", path);
            lock->unlock();
            return false;
        }
        
        char buffer[256];
        size_t bytesRead = fread(buffer, 1, sizeof(buffer) - 1, fp);
        bool readError = ferror(fp) != 0;
        fclose(fp);
        lock->unlock();
        
        if (readError) {
            LOGGER.error("Error reading version file: {}", path);
            return false;
        }
        if (bytesRead == 0) {
            LOGGER.error("Version file is empty: {}", path);
            return false;
        }
        buffer[bytesRead] = '\0';
        content = buffer;
    }
    
    // Parse JSON
    cJSON* json = cJSON_Parse(content.c_str());
    if (json == nullptr) {
        LOGGER.error("Failed to parse version JSON: {}", path);
        return false;
    }
    
    cJSON* versionItem = cJSON_GetObjectItem(json, "version");
    if (versionItem == nullptr || !cJSON_IsNumber(versionItem)) {
        LOGGER.error("Invalid version JSON format: {}", path);
        cJSON_Delete(json);
        return false;
    }
    
    double versionValue = versionItem->valuedouble;
    if (versionValue < 0 || versionValue > UINT32_MAX) {
        LOGGER.error("Version out of valid range [0, {}]: {}", UINT32_MAX, path);
        cJSON_Delete(json);
        return false;
    }
    version.version = static_cast<uint32_t>(versionValue);
    cJSON_Delete(json);
    
    LOGGER.info("Loaded version {} from {}", version.version, path);
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
                LOGGER.error("Failed to create directory: {}", dirPath);
                return false;
            }
        }
    }
    
    // Create JSON
    cJSON* json = cJSON_CreateObject();
    if (json == nullptr) {
        LOGGER.error("Failed to create JSON object for version");
        return false;
    }
    cJSON_AddNumberToObject(json, "version", version.version);
    
    char* jsonString = cJSON_Print(json);
    if (jsonString == nullptr) {
        LOGGER.error("Failed to serialize version JSON");
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
                    LOGGER.error("Failed to flush version file: {}", path);
                    success = false;
                } else {
                    int fd = fileno(fp);
                    if (fd >= 0 && fsync(fd) != 0) {
                        LOGGER.error("Failed to fsync version file: {}", path);
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
        LOGGER.info("Saved version {} to {}", version.version, path);
    } else {
        LOGGER.error("Failed to write version file: {}", path);
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
        LOGGER.error("Max directory depth exceeded: {}", src);
        return false;
    }
    LOGGER.info("Copying directory: {} -> {}", src, dst);
    
    // Create destination directory
    if (!file::isDirectory(dst)) {
        if (!file::findOrCreateDirectory(dst, 0755)) {
            LOGGER.error("Failed to create destination directory: {}", dst);
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
        
        std::string srcPath = std::string(src) + "/" + entry.d_name;
        std::string dstPath = std::string(dst) + "/" + entry.d_name;
        
        if (entry.d_type == file::TT_DT_DIR) {
            // Recursively copy subdirectory
            if (!copyDirectory(srcPath.c_str(), dstPath.c_str(), depth + 1)) {
                copySuccess = false;
            }
        } else if (entry.d_type == file::TT_DT_REG) {
            // Copy file using atomic temp file approach
            auto lock = file::getLock(srcPath);
            lock->lock(portMAX_DELAY);

            // Generate unique temp file path
            uint32_t randomId = esp_random();
            std::string tempPath = dstPath + ".tmp." + std::to_string(randomId);

            FILE* srcFile = fopen(srcPath.c_str(), "rb");
            if (!srcFile) {
                LOGGER.error("Failed to open source file: {}", srcPath);
                lock->unlock();
                copySuccess = false;
                return;
            }

            FILE* tempFile = fopen(tempPath.c_str(), "wb");
            if (!tempFile) {
                LOGGER.error("Failed to create temp file: {}", tempPath);
                fclose(srcFile);
                lock->unlock();
                copySuccess = false;
                return;
            }

            // Copy in chunks
            char buffer[512];
            size_t bytesRead;
            bool fileCopySuccess = true;
            while ((bytesRead = fread(buffer, 1, sizeof(buffer), srcFile)) > 0) {
                size_t bytesWritten = fwrite(buffer, 1, bytesRead, tempFile);
                if (bytesWritten != bytesRead) {
                    LOGGER.error("Failed to write to temp file: {}", tempPath);
                    fileCopySuccess = false;
                    copySuccess = false;
                    break;
                }
            }

            if (fileCopySuccess && ferror(srcFile)) {
                LOGGER.error("Error reading source file: {}", srcPath);
                fileCopySuccess = false;
                copySuccess = false;
            }

            fclose(srcFile);

            // Flush and sync temp file before closing
            if (fileCopySuccess) {
                if (fflush(tempFile) != 0) {
                    LOGGER.error("Failed to flush temp file: {}", tempPath);
                    fileCopySuccess = false;
                    copySuccess = false;
                } else {
                    int fd = fileno(tempFile);
                    if (fd >= 0 && fsync(fd) != 0) {
                        LOGGER.error("Failed to fsync temp file: {}", tempPath);
                        fileCopySuccess = false;
                        copySuccess = false;
                    }
                }
            }

            fclose(tempFile);

            if (fileCopySuccess) {
                // Atomically rename temp file to destination
                if (rename(tempPath.c_str(), dstPath.c_str()) != 0) {
                    LOGGER.error("Failed to rename temp file {} to {}", tempPath, dstPath);
                    remove(tempPath.c_str());
                    fileCopySuccess = false;
                    copySuccess = false;
                }
            } else {
                // Clean up temp file on failure
                remove(tempPath.c_str());
            }

            lock->unlock();

            if (fileCopySuccess) {
                LOGGER.info("Copied file: {}", entry.d_name);
            }
        }
    });
    
    if (!listSuccess) {
        LOGGER.error("Failed to list source directory: {}", src);
        return false;
    }
    
    return copySuccess;
}

bool syncAssets() {
    LOGGER.info("Starting asset synchronization...");

    // Check if Data partition and SD card exist
    bool dataExists = hasDataAssets();
    bool sdExists = hasSdAssets();

    // FIRST BOOT SCENARIO: Data has version 0, SD card is missing
    if (dataExists && !sdExists) {
        LOGGER.info("First boot - Data exists but SD card backup missing");
        LOGGER.warn("Skipping SD backup during boot - will be created on first settings save");
        LOGGER.warn("This avoids watchdog timeout if SD card is slow or corrupted");
        return true;  // Don't block boot - defer copy to runtime
    }

    // NO SD CARD: Just ensure Data has default structure
    if (!sdExists) {
        LOGGER.warn("No SD card available - creating default Data structure if needed");
        if (!dataExists) {
            if (!file::findOrCreateDirectory(DATA_ASSETS_DIR, 0755)) {
                LOGGER.error("Failed to create Data assets directory");
                return false;
            }
            AssetVersion defaultVersion(0);  // Start at version 0 - SD card updates will be version 1+
            if (!saveDataVersion(defaultVersion)) {
                LOGGER.error("Failed to save default Data version");
                return false;
            }
            LOGGER.info("Created default Data assets structure (version 0)");
        }
        return true;
    }

    // POST-FLASH RECOVERY: Data empty but SD card exists
    if (!dataExists) {
        LOGGER.info("Data partition empty - copying from SD card (recovery mode)");
        if (!copyDirectory(SD_ASSETS_DIR, DATA_ASSETS_DIR)) {
            LOGGER.error("Failed to copy assets from SD card to Data");
            return false;
        }
        LOGGER.info("Recovery complete - assets restored from SD card");
        return true;
    }

    // NORMAL OPERATION: Both exist - compare versions
    AssetVersion dataVersion, sdVersion;
    bool hasDataVer = loadDataVersion(dataVersion);
    bool hasSdVer = loadSdVersion(sdVersion);

    if (!hasDataVer) {
        LOGGER.warn("No Data version.json - assuming version 0");
        dataVersion.version = 0;
        if (!saveDataVersion(dataVersion)) {
            LOGGER.warn("Failed to save default Data version (non-fatal)");
        }
    }

    if (!hasSdVer) {
        LOGGER.warn("No SD version.json - assuming version 0");
        sdVersion.version = 0;
        // DON'T save to SD during boot - defer to runtime
        LOGGER.warn("Skipping SD version.json creation during boot - will be created on first settings save");
    }

    LOGGER.info("Version comparison - Data: {}, SD: {}", dataVersion.version, sdVersion.version);

    if (sdVersion.version > dataVersion.version) {
        // Firmware update - copy SD -> Data
        LOGGER.info("SD card newer (v{} > v{}) - copying assets SD -> Data (firmware update)",
                 sdVersion.version, dataVersion.version);
        if (!copyDirectory(SD_ASSETS_DIR, DATA_ASSETS_DIR)) {
            LOGGER.error("Failed to copy assets from SD to Data");
            return false;
        }
        LOGGER.info("Firmware update complete - assets updated from SD card");
    } else if (dataVersion.version > sdVersion.version) {
        // User customization - backup Data -> SD
        LOGGER.warn("Data newer (v{} > v{}) - deferring SD backup to avoid boot watchdog",
                 dataVersion.version, sdVersion.version);
        LOGGER.warn("SD backup will occur on first WebServer settings save");
        return true;  // Don't block boot - defer copy to runtime
    } else {
        LOGGER.info("Versions match (v{}) - no sync needed", dataVersion.version);
    }

    return true;
}

} // namespace

#endif
