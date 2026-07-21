#include <Tactility/app/AppPaths.h>

#include <Tactility/app/AppManifest.h>
#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>

#include <format>

#ifdef ESP_PLATFORM
constexpr auto PARTITION_PREFIX = std::string("/");
#else
constexpr auto PARTITION_PREFIX = std::string("");
#endif

namespace tt::app {

std::string AppPaths::getUserDataPath() const {
    if (manifest.appLocation.isInternal()) {
        return std::format("{}{}/tactility/user/app/{}", PARTITION_PREFIX, file::DATA_PARTITION_NAME, manifest.appId);
    } else {
        return std::format("{}/tactility/user/app/{}", file::getFirstPathSegment(manifest.appLocation.getPath()), manifest.appId);
    }
}

std::string AppPaths::getUserDataPath(const std::string& childPath) const {
    assert(!childPath.starts_with('/'));
    return std::format("{}/{}", getUserDataPath(), childPath);
}


std::string AppPaths::getAssetsPath() const {
    if (manifest.appLocation.isInternal()) {
        return std::format("{}{}/app/{}/assets", PARTITION_PREFIX, file::SYSTEM_PARTITION_NAME, manifest.appId);
    } else {
        return std::format("{}/assets", manifest.appLocation.getPath());
    }
}

std::string AppPaths::getAssetsPath(const std::string& childPath) const {
    assert(!childPath.starts_with('/'));
    return std::format("{}/{}", getAssetsPath(), childPath);
}

}
