#pragma once

#include <Tactility/service/ServiceManifest.h>

#include <tactility/check.h>
#include <tactility/error.h>
#include <tactility/service/service_paths.h>

#include <string>
#include <memory>

namespace tt::service {

constexpr size_t PATH_BUFFER_SIZE = 255;

class ServicePaths {

    std::shared_ptr<const ServiceManifest> manifest;

public:

    explicit ServicePaths(std::shared_ptr<const ServiceManifest> manifest) : manifest(std::move(manifest)) {}

    /**
     * The user data directory is intended to survive OS upgrades.
     * The path will not end with a "/".
     */
    std::string getUserDataDirectory() const {
        char buffer[PATH_BUFFER_SIZE];
        check(service_paths_get_user_data_directory(manifest->id.c_str(), buffer, sizeof(buffer)) == ERROR_NONE);
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
        check(service_paths_get_user_data_path(manifest->id.c_str(), childPath.c_str(), buffer, sizeof(buffer)) == ERROR_NONE);
        return buffer;
    }

    /**
     * You should not store configuration data here.
     * The path will not end with a "/".
     */
    std::string getAssetsDirectory() const {
        char buffer[PATH_BUFFER_SIZE];
        check(service_paths_get_assets_directory(manifest->id.c_str(), buffer, sizeof(buffer)) == ERROR_NONE);
        return buffer;
    }

    /**
     * You should not store configuration data here.
     * @param[in] childPath the path without a "/" prefix
     */
    std::string getAssetsPath(const std::string& childPath) const {
        assert(!childPath.starts_with('/'));
        char buffer[PATH_BUFFER_SIZE];
        check(service_paths_get_assets_path(manifest->id.c_str(), childPath.c_str(), buffer, sizeof(buffer)) == ERROR_NONE);
        return buffer;
    }
};

}