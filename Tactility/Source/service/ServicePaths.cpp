#include <Tactility/service/ServicePaths.h>

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/Paths.h>

#include <cassert>
#include <format>

namespace tt::service {

std::string ServicePaths::getUserDataDirectory() const {
    return std::format("{}/service/{}", tt::getUserDataPath(), manifest->id);
}

std::string ServicePaths::getUserDataPath(const std::string& childPath) const {
    assert(!childPath.starts_with('/'));
    return std::format("{}/{}", getUserDataDirectory(), childPath);
}

std::string ServicePaths::getAssetsDirectory() const {
    return std::format("{}/service/{}/assets", tt::getUserDataPath(), manifest->id);
}

std::string ServicePaths::getAssetsPath(const std::string& childPath) const {
    assert(!childPath.starts_with('/'));
    return std::format("{}/{}", getAssetsDirectory(), childPath);
}

}
