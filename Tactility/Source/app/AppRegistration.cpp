#include <Tactility/app/AppRegistration.h>
#include <Tactility/app/AppManifest.h>

#include <Tactility/Mutex.h>

#include <unordered_map>
#include <Tactility/file/File.h>
#include <tactility/log.h>

namespace tt::app {

constexpr auto* TAG = "AppRegistration";

typedef std::unordered_map<std::string, std::shared_ptr<AppManifest>> AppManifestMap;

static AppManifestMap app_manifest_map;
static Mutex hash_mutex;

void addAppManifest(const AppManifest& manifest) {
    LOG_I(TAG, "Registering manifest %s", manifest.appId.c_str());

    hash_mutex.lock();

    if (app_manifest_map.contains(manifest.appId)) {
        LOG_W(TAG, "Overwriting existing manifest for %s", manifest.appId.c_str());
    }

    app_manifest_map[manifest.appId] = std::make_shared<AppManifest>(manifest);

    hash_mutex.unlock();
}

bool removeAppManifest(const std::string& id) {
    LOG_I(TAG, "Removing manifest for %s", id.c_str());

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
