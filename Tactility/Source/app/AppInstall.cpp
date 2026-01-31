#include <Tactility/app/App.h>
#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <Tactility/file/PropertiesFile.h>
#include <tactility/hal/Device.h>
#include <Tactility/Logger.h>
#include <Tactility/Paths.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <unistd.h>

#include <minitar.h>

namespace tt::app {

static const auto LOGGER = Logger("App");

static bool untarFile(minitar* mp, const minitar_entry* entry, const std::string& destinationPath) {
    const auto absolute_path = destinationPath + "/" + entry->metadata.path;
    if (!file::findOrCreateDirectory(destinationPath, 0777)) {
        LOGGER.error("Can't find or create directory {}", destinationPath.c_str());
        return false;
    }

    // minitar_read_contents(&mp, &entry, file_buffer, entry.metadata.size);
    if (!minitar_read_contents_to_file(mp, entry, absolute_path.c_str())) {
        LOGGER.error("Failed to write data to {}", absolute_path.c_str());
        return false;
    }

    // Note: fchmod() doesn't exist on ESP-IDF and chmod() does nothing on that platform
    if (chmod(absolute_path.c_str(), entry->metadata.mode) < 0) {
        return false;
    }

    return true;
}

static bool untarDirectory(const minitar_entry* entry, const std::string& destinationPath) {
    auto absolute_path = destinationPath + "/" + entry->metadata.path;
    if (!file::findOrCreateDirectory(absolute_path, 0777)) return false;
    return true;
}

static bool untar(const std::string& tarPath, const std::string& destinationPath) {
    minitar mp;
    if (minitar_open(tarPath.c_str(), &mp) != 0) {
        perror(tarPath.c_str());
        return 1;
    }
    bool success = true;
    minitar_entry entry;

    do {
        if (minitar_read_entry(&mp, &entry) == 0) {
            LOGGER.info("Extracting {}", entry.metadata.path);
            if (entry.metadata.type == MTAR_DIRECTORY) {
                if (!strcmp(entry.metadata.name, ".") || !strcmp(entry.metadata.name, "..") || !strcmp(entry.metadata.name, "/")) continue;
                if (!untarDirectory(&entry, destinationPath)) {
                    LOGGER.error("Failed to create directory {}/{}: {}", destinationPath, entry.metadata.name, strerror(errno));
                    success = false;
                    break;
                }
            } else if (entry.metadata.type == MTAR_REGULAR) {
                if (!untarFile(&mp, &entry, destinationPath)) {
                    LOGGER.error("Failed to extract file {}: {}", entry.metadata.path, strerror(errno));
                    success = false;
                    break;
                }
            } else if (entry.metadata.type == MTAR_SYMLINK) {
                LOGGER.error("SYMLINK not supported");
            } else if (entry.metadata.type == MTAR_HARDLINK) {
                LOGGER.error("HARDLINK not supported");
            } else if (entry.metadata.type == MTAR_FIFO) {
                LOGGER.error("FIFO not supported");
            } else if (entry.metadata.type == MTAR_BLKDEV) {
                LOGGER.error("BLKDEV not supported");
            } else if (entry.metadata.type == MTAR_CHRDEV) {
                LOGGER.error("CHRDEV not supported");
            } else {
                LOGGER.error("Unknown entry type: {}", static_cast<int>(entry.metadata.type));
                success = false;
                break;
            }
        } else break;
    } while (true);
    minitar_close(&mp);
    return success;
}

void cleanupInstallDirectory(const std::string& path) {
    if (!file::deleteRecursively(path)) {
        LOGGER.warn("Failed to delete existing installation at {}", path);
    }
}

bool install(const std::string& path) {
    // We lock and unlock frequently because SPI SD card devices share
    // the lock with the display. We don't want to lock the display for very long.

    auto app_parent_path = getAppInstallPath();
    LOGGER.info("Installing app {} to {}", path, app_parent_path);

    auto filename = file::getLastPathSegment(path);
    const std::string app_target_path = std::format("{}/{}", app_parent_path, filename);
    if (file::isDirectory(app_target_path) && !file::deleteRecursively(app_target_path)) {
        LOGGER.warn("Failed to delete {}", app_target_path);
    }

    if (!file::findOrCreateDirectory(app_target_path, 0777)) {
        LOGGER.info("Failed to create directory {}", app_target_path);
        return false;
    }

    auto target_path_lock = file::getLock(app_parent_path)->asScopedLock();
    auto source_path_lock = file::getLock(path)->asScopedLock();
    target_path_lock.lock();
    source_path_lock.lock();
    LOGGER.info("Extracting app from {} to {}", path, app_target_path);
    if (!untar(path, app_target_path)) {
        LOGGER.error("Failed to extract");
        return false;
    }
    source_path_lock.unlock();
    target_path_lock.unlock();

    auto manifest_path = app_target_path + "/manifest.properties";
    if (!file::isFile(manifest_path)) {
        LOGGER.error("Manifest not found at {}", manifest_path);
        cleanupInstallDirectory(app_target_path);
        return false;
    }

    std::map<std::string, std::string> properties;
    if (!file::loadPropertiesFile(manifest_path, properties)) {
        LOGGER.error("Failed to load manifest at {}", manifest_path);
        cleanupInstallDirectory(app_target_path);
        return false;
    }

    AppManifest manifest;
    if (!parseManifest(properties, manifest)) {
        LOGGER.warn("Invalid manifest");
        cleanupInstallDirectory(app_target_path);
        return false;
    }

    // If the app was already running, then stop it
    if (isRunning(manifest.appId)) {
        stopAll(manifest.appId);
    }

    const std::string renamed_target_path = std::format("{}/{}", app_parent_path, manifest.appId);
    if (file::isDirectory(renamed_target_path)) {
        if (!file::deleteRecursively(renamed_target_path)) {
            LOGGER.warn("Failed to delete existing installation at {}", renamed_target_path);
            cleanupInstallDirectory(app_target_path);
            return false;
        }
    }

    target_path_lock.lock();
    bool rename_success = rename(app_target_path.c_str(), renamed_target_path.c_str()) == 0;
    target_path_lock.unlock();

    if (!rename_success) {
        LOGGER.error(R"(Failed to rename "{}" to "{}")", app_target_path, manifest.appId);
        cleanupInstallDirectory(app_target_path);
        return false;
    }

    manifest.appLocation = Location::external(renamed_target_path);

    addAppManifest(manifest);

    return true;
}

bool uninstall(const std::string& appId) {
    LOGGER.info("Uninstalling app {}", appId);

    // If the app was running, then stop it
    if (isRunning(appId)) {
        stopAll(appId);
    }

    auto app_path = getAppInstallPath(appId);
    if (!file::isDirectory(app_path)) {
        LOGGER.error("App {} not found at {}", appId, app_path);
        return false;
    }

    if (!file::deleteRecursively(app_path)) {
        return false;
    }

    if (!removeAppManifest(appId)) {
        LOGGER.warn("Failed to remove app {} from registry", appId);
    }

    return true;
}

} // namespace