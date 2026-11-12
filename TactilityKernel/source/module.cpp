#include <cstring>
#include <algorithm>
#include <new>
#include <tactility/concurrent/mutex.h>
#include <tactility/module.h>

#include "tactility/driver.h"
#include <tactility/driver_internal.h>

#include <vector>

constexpr auto* TAG = "module";

struct ModuleInternal {
    bool started = false;
    bool drivers_ready = false;
    bool external = false;
    // All three are guarded by the ledger mutex
    int use_count = 0;
    // References from relocated code (see module_get_code()): they keep the module mapped, but
    // they don't keep it running
    int code_count = 0;
    bool stopping = false;
    // In the ledger. Its symbols can be resolved from then on, also while it hasn't started yet.
    bool added = false;
};

struct ModuleLedger {
    std::vector<Module*> modules;
    Mutex mutex {};

    ModuleLedger() { mutex_construct(&mutex); }
    ~ModuleLedger() { mutex_destruct(&mutex); }
};

static ModuleLedger ledger;

extern "C" {

error_t module_construct(Module* module) {
    if (module->internal != nullptr) {
        LOG_E(TAG, "Module %s was already constructed", module->name);
        return ERROR_INVALID_STATE;
    }
    module->internal = new (std::nothrow) ModuleInternal();
    if (module->internal == nullptr) return ERROR_OUT_OF_MEMORY;
    return ERROR_NONE;
}

error_t module_destruct(Module* module) {
    if (module->internal == nullptr) {
        LOG_E(TAG, "Module %s was already destructed", module->name);
        return ERROR_INVALID_STATE;
    }
    delete module->internal;
    module->internal = nullptr;
    return ERROR_NONE;
}

error_t module_add(Module* module) {
    mutex_lock(&ledger.mutex);
    bool exists = false;
    for (auto* ledger_module : ledger.modules) {
        if (ledger_module == module) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        ledger.modules.push_back(module);
        if (module->internal != nullptr) {
            module->internal->added = true;
        }
    }
    mutex_unlock(&ledger.mutex);
    return exists ? ERROR_INVALID_STATE : ERROR_NONE;
}

error_t module_remove(Module* module) {
    mutex_lock(&ledger.mutex);
    // Checked under the same lock as the removal, so no reference can be taken in between
    if (module->internal != nullptr && (module->internal->use_count != 0 || module->internal->code_count != 0)) {
        mutex_unlock(&ledger.mutex);
        return ERROR_RESOURCE_BUSY;
    }
    ledger.modules.erase(std::remove(ledger.modules.begin(), ledger.modules.end(), module), ledger.modules.end());
    if (module->internal != nullptr) {
        module->internal->added = false;
    }
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

error_t module_start(Module* module) {
    LOG_I(TAG, "start %s", module->name);

    auto* internal = module->internal;
    if (internal == nullptr) { return ERROR_INVALID_STATE; }
    if (internal->started) { return ERROR_NONE; }

    if (module->start != nullptr) {
        auto error = module->start();
        if (error != ERROR_NONE) {
            return error;
        }
    }

    if (module->drivers != nullptr && !internal->drivers_ready) {
        auto* driver_location = module->drivers;
        while (*driver_location != nullptr) {
            auto driver = *driver_location;
            check(driver_construct(driver) == ERROR_NONE);
            driver_set_owner(driver, module);
            check(driver_add(driver) == ERROR_NONE);
            driver_location++;
        }
        internal->drivers_ready = true;
    }

    internal->started = true;
    return ERROR_NONE;
}

bool module_is_started(Module* module) {
    auto* internal = module->internal;
    return internal != nullptr && internal->started;
}

bool module_is_external(Module* module) {
    auto* internal = module->internal;
    return internal != nullptr && internal->external;
}

error_t module_set_external(Module* module, bool external) {
    if (module->internal == nullptr) { return ERROR_INVALID_STATE; }
    module->internal->external = external;
    return ERROR_NONE;
}

static void end_stopping(ModuleInternal* internal) {
    mutex_lock(&ledger.mutex);
    internal->stopping = false;
    mutex_unlock(&ledger.mutex);
}

/**
 * A usage reference always blocks stopping. A code reference (from relocated code) doesn't: an app
 * stops a firmware module it was linked against on purpose, to take over the device that module
 * drives, and starts it again when it's done.
 * Modules from packages are the exception: the user can stop those from Kernel settings while an
 * app is running, so there the references from that app must keep it running.
 * Caller must hold the ledger mutex.
 */
static bool is_stop_blocked(const ModuleInternal* internal) {
    return internal->use_count != 0 || (internal->external && internal->code_count != 0);
}

error_t module_stop(Module* module) {
    LOG_I(TAG, "stop %s", module->name);

    auto* internal = module->internal;
    if (internal == nullptr) { return ERROR_INVALID_STATE; }

    // A module that hasn't started can be referenced too, through its symbols
    mutex_lock(&ledger.mutex);
    const bool referenced = is_stop_blocked(internal);
    mutex_unlock(&ledger.mutex);
    if (referenced) {
        LOG_W(TAG, "%s can't stop: it is in use", module->name);
        return ERROR_RESOURCE_BUSY;
    }

    if (!internal->started) { return ERROR_NONE; }

    // Checking and refusing new references is one atomic step, so a module_get() can't slip in afterwards
    mutex_lock(&ledger.mutex);
    if (is_stop_blocked(internal)) {
        mutex_unlock(&ledger.mutex);
        LOG_W(TAG, "%s can't stop: it is in use", module->name);
        return ERROR_RESOURCE_BUSY;
    }
    internal->stopping = true;
    mutex_unlock(&ledger.mutex);

    if (module->drivers != nullptr && internal->drivers_ready) {
        size_t count = 0;
        while (module->drivers[count] != nullptr) {
            count++;
        }

        // Atomic per driver, so a refused stop leaves the module fully running and no bind can slip in afterwards
        for (size_t i = 0; i < count; i++) {
            error_t error = driver_begin_destroy(module->drivers[i]);
            if (error != ERROR_NONE) {
                LOG_W(TAG, "%s can't stop: driver %s is in use", module->name, module->drivers[i]->name);
                for (size_t j = 0; j < i; j++) {
                    driver_cancel_destroy(module->drivers[j]);
                }
                end_stopping(internal);
                return error;
            }
        }

        for (size_t i = count; i-- > 0;) {
            check(driver_remove_destruct(module->drivers[i]) == ERROR_NONE);
        }
        internal->drivers_ready = false;
    }

    if (module->stop != nullptr) {
        auto error = module->stop();
        if (error != ERROR_NONE) {
            end_stopping(internal);
            return error;
        }
    }

    internal->started = false;
    end_stopping(internal);
    return ERROR_NONE;
}

error_t module_construct_add_start(Module* module) {
    error_t error = module_construct(module);
    if (error != ERROR_NONE) { return error; }
    error = module_add(module);
    if (error != ERROR_NONE) { return error; }
    return module_start(module);
}

error_t module_ensure_started(Module* module) {
    if (module->internal == nullptr) {
        error_t result = module_construct(module);
        if (result != ERROR_NONE) { return result; }
    }

    error_t add_result = module_add(module);
    if (add_result != ERROR_NONE && add_result != ERROR_INVALID_STATE) { return add_result; }

    if (!module->internal->started) {
        error_t result = module_start(module);
        if (result != ERROR_NONE) { return result; }
    }

    return ERROR_NONE;
}

error_t module_ensure_destructed(Module* module) {
    if (module->internal != nullptr) {
        // Also when it hasn't started, because its symbols can be in use
        error_t result = module_stop(module);
        if (result != ERROR_NONE) { return result; }

        result = module_remove(module);
        if (result != ERROR_NONE) { return result; }

        result = module_destruct(module);
        if (result != ERROR_NONE) { return result; }
    }

    return ERROR_NONE;
}

error_t module_get(Module* module) {
    auto* internal = module->internal;
    if (internal == nullptr) { return ERROR_INVALID_STATE; }

    mutex_lock(&ledger.mutex);
    const bool available = internal->started && !internal->stopping;
    if (available) {
        internal->use_count++;
    }
    mutex_unlock(&ledger.mutex);
    return available ? ERROR_NONE : ERROR_INVALID_STATE;
}

void module_put(Module* module) {
    mutex_lock(&ledger.mutex);
    check(module->internal != nullptr && module->internal->use_count > 0);
    module->internal->use_count--;
    mutex_unlock(&ledger.mutex);
}

error_t module_get_code(Module* module) {
    auto* internal = module->internal;
    if (internal == nullptr) { return ERROR_INVALID_STATE; }

    mutex_lock(&ledger.mutex);
    // Also allowed while the module isn't started: relocated code needs the symbols, not the state
    const bool available = internal->added && !internal->stopping;
    if (available) {
        internal->code_count++;
    }
    mutex_unlock(&ledger.mutex);
    return available ? ERROR_NONE : ERROR_INVALID_STATE;
}

void module_put_code(Module* module) {
    mutex_lock(&ledger.mutex);
    check(module->internal != nullptr && module->internal->code_count > 0);
    module->internal->code_count--;
    mutex_unlock(&ledger.mutex);
}

bool module_resolve_symbol(Module* module, const char* symbol_name, uintptr_t* symbol_address) {
    if (module->internal == nullptr || !module->internal->added) return false;
    auto* symbol_ptr = module->symbols;
    if (symbol_ptr == nullptr) return false;
    while (symbol_ptr->name != nullptr) {
        if (strcmp(symbol_ptr->name, symbol_name) == 0) {
            *symbol_address = reinterpret_cast<uintptr_t>(symbol_ptr->symbol);
            return true;
        }
        symbol_ptr++;
    }
    return false;
}

enum class RefKind { NONE, USE, CODE };

static bool resolve_symbol_global(const char* symbol_name, uintptr_t* symbol_address, Module** out_module, RefKind ref_kind) {
    mutex_lock(&ledger.mutex);
    for (auto* module : ledger.modules) {
        auto* internal = module->internal;
        if (internal == nullptr) {
            continue;
        }
        // A module that's being stopped may still be resolvable, but no new reference may be taken
        if (ref_kind != RefKind::NONE && internal->stopping) {
            continue;
        }
        if (module_resolve_symbol(module, symbol_name, symbol_address)) {
            if (ref_kind == RefKind::USE) {
                internal->use_count++;
            } else if (ref_kind == RefKind::CODE) {
                internal->code_count++;
            }
            mutex_unlock(&ledger.mutex);
            if (out_module != nullptr) {
                *out_module = module;
            }
            return true;
        }
    }
    mutex_unlock(&ledger.mutex);
    return false;
}

bool module_resolve_symbol_global(const char* symbol_name, uintptr_t* symbol_address) {
    return resolve_symbol_global(symbol_name, symbol_address, nullptr, RefKind::NONE);
}

bool module_resolve_symbol_global_get(const char* symbol_name, uintptr_t* symbol_address, Module** out_module) {
    return resolve_symbol_global(symbol_name, symbol_address, out_module, RefKind::USE);
}

bool module_resolve_symbol_global_get_code(const char* symbol_name, uintptr_t* symbol_address, Module** out_module) {
    return resolve_symbol_global(symbol_name, symbol_address, out_module, RefKind::CODE);
}

}
