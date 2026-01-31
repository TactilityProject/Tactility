#include <vector>
#include <algorithm>
#include <tactility/concurrent/mutex.h>
#include <tactility/module.h>

#define TAG LOG_TAG(module)

struct ModuleParentPrivate {
    std::vector<struct Module*> modules;
    struct Mutex mutex = { 0 };
};

extern "C" {

#pragma region module_parent

error_t module_parent_construct(struct ModuleParent* parent) {
    parent->module_parent_private = new (std::nothrow) ModuleParentPrivate();
    if (!parent->module_parent_private) return ERROR_OUT_OF_MEMORY;

    auto* data = static_cast<ModuleParentPrivate*>(parent->module_parent_private);
    mutex_construct(&data->mutex);

    return ERROR_NONE;
}

error_t module_parent_destruct(struct ModuleParent* parent) {
    auto* data = static_cast<ModuleParentPrivate*>(parent->module_parent_private);
    if (data == nullptr) return ERROR_NONE;

    mutex_lock(&data->mutex);
    if (!data->modules.empty()) {
        mutex_unlock(&data->mutex);
        return ERROR_INVALID_STATE;
    }
    mutex_unlock(&data->mutex);

    mutex_destruct(&data->mutex);
    delete data;
    parent->module_parent_private = nullptr;
    return ERROR_NONE;
}

#pragma endregion

#pragma region module

error_t module_set_parent(struct Module* module, struct ModuleParent* parent) {
    if (module->internal.started) return ERROR_INVALID_STATE;
    if (module->internal.parent == parent) return ERROR_NONE;

    // Remove from old parent
    if (module->internal.parent && module->internal.parent->module_parent_private) {
        auto* old_data = static_cast<ModuleParentPrivate*>(module->internal.parent->module_parent_private);
        mutex_lock(&old_data->mutex);
        auto it = std::find(old_data->modules.begin(), old_data->modules.end(), module);
        if (it != old_data->modules.end()) {
            old_data->modules.erase(it);
        }
        mutex_unlock(&old_data->mutex);
    }

    module->internal.parent = parent;

    // Add to new parent
    if (parent && parent->module_parent_private) {
        auto* new_data = static_cast<ModuleParentPrivate*>(parent->module_parent_private);
        mutex_lock(&new_data->mutex);
        new_data->modules.push_back(module);
        mutex_unlock(&new_data->mutex);
    }

    return ERROR_NONE;
}

error_t module_start(struct Module* module) {
    LOG_I(TAG, "start %s", module->name);

    if (module->internal.started) return ERROR_NONE;
    if (!module->internal.parent) return ERROR_INVALID_STATE;

    error_t error = module->start();
    module->internal.started = (error == ERROR_NONE);
    return error;
}

bool module_is_started(struct Module* module) {
    return module->internal.started;
}

error_t module_stop(struct Module* module) {
    LOG_I(TAG, "stop %s", module->name);

    if (!module->internal.started) return ERROR_NONE;

    error_t error = module->stop();
    if (error != ERROR_NONE) {
        return error;
    }

    module->internal.started = false;
    return error;
}

#pragma endregion

}

