#include <Tactility/DeprecatedPaths.h>

#include <app/manifest.h>

#include <Tactility/MountPoints.h>

#include <format>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/paths.h>

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
        auto* owner = file_system_get_owner(fs);
        if (owner == nullptr || device_get_type(owner) != &SDCARD_TYPE) {
            return true;
        }
        *static_cast<FileSystem**>(context) = fs;
        return false;
    });
    if (found && mustBeMounted && !file_system_is_mounted(found)) {
        return nullptr;
    }
    return found;
}

std::string getUserDataRootPath() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    check(paths_get_root_path(path, sizeof(path)) == ERROR_NONE);
    return std::string(path);
}

std::string getDataPath() {
    return getUserDataRootPath() + "/tactility";
}

std::string getTempPath() {
    return getDataPath() + "/tmp";
}

std::string getAppInstallPath() {
    return getDataPath() + "/app";
}

std::string getUserHomePath() {
    return getDataPath() + "/user";
}

std::string getAppInstallPath(const std::string& appId) {
    assert(app_manifest_id_is_valid(appId.c_str()));
    return std::format("{}/{}", getAppInstallPath(), appId);
}

std::string getAppUserPath(const std::string& appId) {
    assert(app_manifest_id_is_valid(appId.c_str()));
    return std::format("{}/appdata/{}", getDataPath(), appId);
}

}