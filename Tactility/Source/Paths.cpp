#include <Tactility/Paths.h>

#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/MountPoints.h>

#include <format>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/sdcard.h>
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
#ifdef CONFIG_TT_USER_DATA_LOCATION_INTERNAL
    return file::MOUNT_POINT_DATA;
#elif CONFIG_TT_USER_DATA_LOCATION_SD
    auto* fs = findSdcardFileSystem(false);
    check(fs);
    char fs_path[32];
    check(file_system_get_path(fs, fs_path, sizeof(fs_path)) == ERROR_NONE);
    return std::string(fs_path);
#else
#error CONFIG_TT_USER_DATA_* not set or unsupported
#endif
}

std::string getUserDataPath() {
#ifdef ESP_PLATFORM
    return getUserDataRootPath() + "/tactility";
#else
    return "data";
#endif
}

std::string getTempPath() {
    return getUserDataPath() + "/tmp";
}

std::string getAppInstallPath() {
    return getUserDataPath() + "/app";
}

std::string getUserHomePath() {
    return getUserDataPath() + "/user";
}

std::string getAppInstallPath(const std::string& appId) {
    assert(app::isValidId(appId));
    return std::format("{}/{}", getAppInstallPath(), appId);
}

std::string getAppUserPath(const std::string& appId) {
    assert(app::isValidId(appId));
    return std::format("{}/app/{}", getUserHomePath(), appId);
}

}