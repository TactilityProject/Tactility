#include <Tactility/app/AppRegistration.h>
#include <Tactility/app/AppManifest.h>

#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>

#include <unordered_map>
#include <Tactility/file/File.h>

namespace tt::app {

static const auto LOGGER = Logger("AppRegistration");

typedef std::unordered_map<std::string, std::shared_ptr<AppManifest>> AppManifestMap;

static AppManifestMap app_manifest_map;
static Mutex hash_mutex;

void addAppManifest(const AppManifest& manifest) {
    LOGGER.info("Registering manifest {}", manifest.appId);

    hash_mutex.lock();

    if (app_manifest_map.contains(manifest.appId)) {
        LOGGER.warn("Overwriting existing manifest for {}", manifest.appId);
    }

    app_manifest_map[manifest.appId] = std::make_shared<AppManifest>(manifest);

    hash_mutex.unlock();
}

bool removeAppManifest(const std::string& id) {
    LOGGER.info("Removing manifest for {}", id);

    auto lock = hash_mutex.asScopedLock();
    lock.lock();

    return app_manifest_map.erase(id) == 1;
}

std::shared_ptr<AppManifest> findAppManifestById(const std::string& id) {
    hash_mutex.lock();
    auto result = app_manifest_map.find(id);
    hash_mutex.unlock();
    if (result != app_manifest_map.end()) {
        return result->second;
    } else {
        return nullptr;
    }
}

std::vector<std::shared_ptr<AppManifest>> getAppManifests() {
    std::vector<std::shared_ptr<AppManifest>> manifests;
    hash_mutex.lock();
    for (const auto& item: app_manifest_map) {
        manifests.push_back(item.second);
    }
    hash_mutex.unlock();
    return manifests;
}

} // namespace
