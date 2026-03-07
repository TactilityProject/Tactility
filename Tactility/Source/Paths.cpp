#include <Tactility/Paths.h>

#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/MountPoints.h>

#include <format>
#include <tactility/filesystem/file_system.h>

namespace tt {

bool findFirstMountedSdCardPath(std::string& path) {
    auto* fs = findSdcardFileSystem(true);
    if (fs == nullptr) return false;
    char found_path[128];
    if (file_system_get_path(fs, found_path, sizeof(found_path)) != ERROR_NONE) return false;
    path = found_path;
    return true;
}

FileSystem* findSdcardFileSystem(bool mustBeMounted) {
    FileSystem* found = nullptr;
    file_system_for_each(&found, [](auto* fs, void* context) {
        char path[128];
        if (file_system_get_path(fs, path, sizeof(path)) != ERROR_NONE) return true;
        // TODO: Find a better way to identify SD card paths
        if (std::string(path).starts_with("/sdcard")) {
            *static_cast<FileSystem**>(context) = fs;
            return false;
        }
        return true;
    });
    if (found && mustBeMounted && !file_system_is_mounted(found)) {
        return nullptr;
    }
    return found;
}

std::string getSystemRootPath() {
    std::string root_path;
    if (!findFirstMountedSdCardPath(root_path)) {
        root_path = file::MOUNT_POINT_DATA;
    }
    return root_path;
}

std::string getTempPath() {
    return getSystemRootPath() + "/tmp";
}

std::string getAppInstallPath() {
    return getSystemRootPath() + "/app";
}

std::string getUserPath() {
    return getSystemRootPath() + "/user";
}

std::string getAppInstallPath(const std::string& appId) {
    assert(app::isValidId(appId));
    return std::format("{}/{}", getAppInstallPath(), appId);
}

std::string getAppUserPath(const std::string& appId) {
    assert(app::isValidId(appId));
    return std::format("{}/app/{}", getUserPath(), appId);
}

}