// SPDX-License-Identifier: Apache-2.0
#include <app/install.h>
#include <app/loader.h>
#include <app/private/binary_path.h>
#include <app/private/module_loader.h>

#include <tactility/Memory.hpp>

#include <service/manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <cstring>
#include <unordered_map>

constexpr auto* TAG = "app_module_loader";

namespace {

struct LoadedModule {
    AppRuntime runtime;
    Module* module;
};

struct LoadedPackage {
    std::string install_dir;
    std::vector<LoadedModule> modules;
};

struct LoadedModules {
    std::unordered_map<std::string, LoadedPackage> packages;
    Mutex mutex {};

    LoadedModules() { mutex_construct(&mutex); }
};

LoadedModules& loaded_modules() {
    static LoadedModules* instance = tt::newObject<LoadedModules>();
    check(instance != nullptr);
    return *instance;
}

const AppLoaderApi* find_path_loader() {
    ServiceInstance* instance = service_manager_find_instance(APP_LOADER_PATH_SERVICE_ID);
    if (instance == nullptr) {
        return nullptr;
    }
    return static_cast<const AppLoaderApi*>(service_instance_get_data(instance));
}

// The binary is only unloaded once the module is gone, because its code is still running before that
error_t unload_module(const AppLoaderApi& loader, const LoadedModule& loaded) {
    error_t error = module_ensure_destructed(loaded.module);
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Module %s can't be unloaded: %s", loaded.module->name, error_to_string(error));
        return error;
    }
    loader.unload(loaded.runtime);
    return ERROR_NONE;
}

error_t load_module(const AppLoaderApi& loader, const std::string& path, LoadedModule& out_loaded) {
    AppLocation location = { APP_LOCATION_PATH, const_cast<char*>(path.c_str()) };

    AppRuntime runtime = nullptr;
    error_t error = loader.load(location, &runtime);
    if (error != ERROR_NONE) {
        return error;
    }

    Module* module = nullptr;
    error = loader.get_module(runtime, &module);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "%s did not return a module", path.c_str());
        loader.unload(runtime);
        return error;
    }

    out_loaded = { runtime, module };
    error = module_construct(module);
    if (error == ERROR_NONE) {
        module_set_external(module, true);
        error = module_add(module);
    }
    if (error == ERROR_NONE) {
        error = module_start(module);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start module %s: %s", module->name, error_to_string(error));
        unload_module(loader, out_loaded);
        return error;
    }

    return ERROR_NONE;
}

} // namespace

error_t app_module_loader_load_package(const std::string& package_id, const std::string& install_dir, const std::vector<std::string>& binaries) {
    if (binaries.empty()) {
        return ERROR_NONE;
    }

    const AppLoaderApi* loader = find_path_loader();
    if (loader == nullptr || loader->get_module == nullptr) {
        LOG_E(TAG, "The path loader can't load modules");
        return ERROR_NOT_SUPPORTED;
    }

    auto& registry = loaded_modules();
    mutex_lock(&registry.mutex);
    if (registry.packages.contains(package_id)) {
        mutex_unlock(&registry.mutex);
        LOG_E(TAG, "Modules of %s are already loaded", package_id.c_str());
        return ERROR_INVALID_STATE;
    }

    std::vector<LoadedModule> loaded;
    for (const auto& binary : binaries) {
        LoadedModule entry {};
        error_t error = load_module(*loader, app_resolve_binary_path(install_dir, binary), entry);
        if (error != ERROR_NONE) {
            // Modules that refuse to stop stay loaded and registered
            std::vector<LoadedModule> stuck;
            for (auto iterator = loaded.rbegin(); iterator != loaded.rend(); ++iterator) {
                if (unload_module(*loader, *iterator) != ERROR_NONE) {
                    stuck.push_back(*iterator);
                }
            }
            if (!stuck.empty()) {
                registry.packages.emplace(package_id, LoadedPackage { install_dir, std::move(stuck) });
            }
            mutex_unlock(&registry.mutex);
            return error;
        }
        loaded.push_back(entry);
    }

    registry.packages.emplace(package_id, LoadedPackage { install_dir, std::move(loaded) });
    mutex_unlock(&registry.mutex);
    return ERROR_NONE;
}

error_t app_module_loader_unload_package(const std::string& package_id) {
    auto& registry = loaded_modules();
    mutex_lock(&registry.mutex);
    auto iterator = registry.packages.find(package_id);
    if (iterator == registry.packages.end()) {
        mutex_unlock(&registry.mutex);
        return ERROR_NONE;
    }

    const AppLoaderApi* loader = find_path_loader();
    check(loader != nullptr);

    // Reverse order, and the first module that can't stop keeps itself and the ones before it loaded
    auto& modules = iterator->second.modules;
    error_t error = ERROR_NONE;
    while (!modules.empty() && error == ERROR_NONE) {
        error = unload_module(*loader, modules.back());
        if (error == ERROR_NONE) {
            modules.pop_back();
        }
    }
    if (modules.empty()) {
        registry.packages.erase(iterator);
    }

    mutex_unlock(&registry.mutex);
    return error;
}

extern "C" void app_module_for_each(AppModuleVisitorFn visitor, void* context) {
    auto& registry = loaded_modules();
    mutex_lock(&registry.mutex);
    for (const auto& [package_id, package] : registry.packages) {
        for (const auto& loaded : package.modules) {
            AppModuleInfo info = { package_id.c_str(), package.install_dir.c_str(), loaded.module };
            visitor(&info, context);
        }
    }
    mutex_unlock(&registry.mutex);
}

extern "C" error_t app_module_set_started(const char* package_id, const char* module_name, bool start) {
    auto& registry = loaded_modules();
    mutex_lock(&registry.mutex);

    error_t error = ERROR_NOT_FOUND;
    auto iterator = registry.packages.find(package_id);
    if (iterator != registry.packages.end()) {
        for (const auto& loaded : iterator->second.modules) {
            if (strcmp(loaded.module->name, module_name) == 0) {
                error = start ? module_start(loaded.module) : module_stop(loaded.module);
                break;
            }
        }
    }

    mutex_unlock(&registry.mutex);
    return error;
}
