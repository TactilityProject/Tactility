#pragma once

#include <tactility/check.h>
#include <tactility/error.h>
#include <tactility/service/service_manifest.h>
#include <tactility/service/service_paths.h>

#include <cassert>
#include <string>

namespace tt::service {

constexpr size_t PATH_BUFFER_SIZE = 255;

class ServicePaths {

    const ::ServiceManifest* manifest;

public:

    explicit ServicePaths(const ::ServiceManifest* manifest) : manifest(manifest) {}

    /**
     * The user data directory is intended to survive OS upgrades.
     * The path will not end with a "/".
     */
    std::string getUserDataDirectory() const {
        char buffer[PATH_BUFFER_SIZE];
        check(service_paths_get_user_data_directory(manifest->id, buffer, sizeof(buffer)) == ERROR_NONE);
        return buffer;
    }

    /**
     * The user data directory is intended to survive OS upgrades.
     * Configuration data should be stored here.
     * @param[in] childPath the path without a "/" prefix
     */
    std::string getUserDataPath(const std::string& childPath) const {
        assert(!childPath.starts_with('/'));
        char buffer[PATH_BUFFER_SIZE];
        check(service_paths_get_user_data_path(manifest->id, childPath.c_str(), buffer, sizeof(buffer)) == ERROR_NONE);
        return buffer;
    }

    /**
     * You should not store configuration data here.
     * The path will not end with a "/".
     */
    std::string getAssetsDirectory() const {
        char buffer[PATH_BUFFER_SIZE];
        check(service_paths_get_assets_directory(manifest->id, buffer, sizeof(buffer)) == ERROR_NONE);
        return buffer;
    }

    /**
     * You should not store configuration data here.
     * @param[in] childPath the path without a "/" prefix
     */
    std::string getAssetsPath(const std::string& childPath) const {
        assert(!childPath.starts_with('/'));
        char buffer[PATH_BUFFER_SIZE];
        check(service_paths_get_assets_path(manifest->id, childPath.c_str(), buffer, sizeof(buffer)) == ERROR_NONE);
        return buffer;
    }
};

}
