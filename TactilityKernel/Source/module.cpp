#include <vector>
#include <string.h>
#include <algorithm>
#include <tactility/concurrent/mutex.h>
#include <tactility/module.h>

#define TAG "module"

struct ModuleLedger {
    std::vector<struct Module*> modules;
    struct Mutex mutex = { 0 };

    ModuleLedger() { mutex_construct(&mutex); }
    ~ModuleLedger() { mutex_destruct(&mutex); }
};

static ModuleLedger ledger;

extern "C" {

error_t module_construct(struct Module* module) {
    module->internal.started = false;
    return ERROR_NONE;
}

error_t module_destruct(struct Module* module) {
    return ERROR_NONE;
}

error_t module_add(struct Module* module) {
    mutex_lock(&ledger.mutex);
    ledger.modules.push_back(module);
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

error_t module_remove(struct Module* module) {
    mutex_lock(&ledger.mutex);
    ledger.modules.erase(std::remove(ledger.modules.begin(), ledger.modules.end(), module), ledger.modules.end());
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

error_t module_start(struct Module* module) {
    LOG_I(TAG, "start %s", module->name);

    if (module->internal.started) return ERROR_NONE;

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

bool module_resolve_symbol(Module* module, const char* symbol_name, uintptr_t* symbol_address) {
    if (!module_is_started(module)) return false;
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

bool module_resolve_symbol_global(const char* symbol_name, uintptr_t* symbol_address) {
    mutex_lock(&ledger.mutex);
    for (auto* module : ledger.modules) {
        if (!module_is_started(module))
            continue;
        if (module_resolve_symbol(module, symbol_name, symbol_address)) {
            mutex_unlock(&ledger.mutex);
            return true;
        }
    }
    mutex_unlock(&ledger.mutex);
    return false;
}

}

