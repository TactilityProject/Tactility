#include <cstring>
#include <algorithm>
#include <new>
#include <tactility/concurrent/mutex.h>
#include <tactility/memory.h>
#include <tactility/module.h>

#include "tactility/driver.h"
#include <tactility/driver_internal.h>

#include <vector>

constexpr auto* TAG = "module";

struct ModuleSymbolIndex {
    // Power-of-two bucket count, or 0 when the module isn't indexed.
    size_t bucket_count = 0;
    // 1-based symbol index per bucket, 0 when the bucket is empty.
    uint16_t* buckets = nullptr;
    // 1-based next symbol index in the same bucket, 0 at the end of the chain.
    uint16_t* chain = nullptr;
};

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
    // Name lookup for this module's symbols, so a global resolve doesn't strcmp every symbol.
    // Points into the ledger's arena while an ELF is relocated, and is empty otherwise.
    ModuleSymbolIndex symbol_index;
};

// FNV-1a. Symbol names are short, and the hash only picks a bucket.
static uint32_t hash_symbol_name(const char* name) {
    uint32_t hash = 2166136261u;
    while (*name != '\0') {
        hash ^= static_cast<uint8_t>(*name++);
        hash *= 16777619u;
    }
    return hash;
}

// Number of symbols and buckets for a module's index, or false when it shouldn't be indexed.
static bool symbol_index_shape(const struct ModuleSymbol* symbols, size_t& count, size_t& bucket_count) {
    if (symbols == nullptr) return false;

    count = 0;
    while (symbols[count].name != nullptr) count++;
    if (count == 0 || count > UINT16_MAX) {
        // Empty, or too large for 16-bit indices: keep the linear scan.
        return false;
    }

    size_t requested = count < 64 ? 64 : count;
    bucket_count = 1;
    while (bucket_count < requested) bucket_count <<= 1;
    return true;
}

static void build_symbol_index(
    ModuleSymbolIndex& index,
    const struct ModuleSymbol* symbols,
    size_t count,
    size_t bucket_count,
    uint16_t*& cursor
) {
    index.buckets = cursor;
    cursor += bucket_count;
    index.chain = cursor;
    cursor += count;
    index.bucket_count = bucket_count;
    memset(index.buckets, 0, bucket_count * sizeof(uint16_t));

    // Built in reverse so a bucket chain follows the table order: if a module exports the same
    // name twice (an alias), the first one still wins, as it does with the linear scan.
    for (size_t i = count; i-- > 0;) {
        const uint32_t bucket = hash_symbol_name(symbols[i].name) & (bucket_count - 1);
        index.chain[i] = index.buckets[bucket];
        index.buckets[bucket] = static_cast<uint16_t>(i + 1);
    }
}

struct ModuleLedger {
    std::vector<Module*> modules;
    Mutex mutex {};
    // One allocation for every module index while an ELF is relocated; null when idle.
    uint16_t* symbol_index_arena = nullptr;
    // module_symbol_index_acquire()/release() nesting
    int symbol_index_users = 0;

    ModuleLedger() { mutex_construct(&mutex); }
    ~ModuleLedger() { mutex_destruct(&mutex); }
};

static ModuleLedger ledger;

// The index is a relocation-time optimization: keep it out of internal RAM by requiring
// external memory, and skip it on ESP devices that don't have external memory at all.
// If the allocation fails, the linear scan remains.
static constexpr MemoryPolicy SYMBOL_INDEX_POLICY = {
    .required = MEMORY_CAPABILITY_EXTERNAL,
    .desired = 0,
    .alignment = 0,
};

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
        // The index (if any) lives in the ledger's arena, which a later release frees.
        module->internal->symbol_index = {};
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

// Callers that already hold the ledger mutex use this; the public wrapper below locks.
static bool module_resolve_symbol_locked(Module* module, const char* symbol_name, uintptr_t* symbol_address) {
    if (module->internal == nullptr || !module->internal->added) return false;
    auto* symbol_ptr = module->symbols;
    if (symbol_ptr == nullptr) return false;

    const auto& index = module->internal->symbol_index;
    if (index.buckets != nullptr) {
        const uint32_t bucket = hash_symbol_name(symbol_name) & (index.bucket_count - 1);
        for (uint16_t i = index.buckets[bucket]; i != 0; i = index.chain[i - 1]) {
            const auto& symbol = symbol_ptr[i - 1];
            if (symbol.name != nullptr && strcmp(symbol.name, symbol_name) == 0) {
                *symbol_address = reinterpret_cast<uintptr_t>(symbol.symbol);
                return true;
            }
        }
        return false;
    }

    while (symbol_ptr->name != nullptr) {
        if (strcmp(symbol_ptr->name, symbol_name) == 0) {
            *symbol_address = reinterpret_cast<uintptr_t>(symbol_ptr->symbol);
            return true;
        }
        symbol_ptr++;
    }
    return false;
}

bool module_resolve_symbol(Module* module, const char* symbol_name, uintptr_t* symbol_address) {
    mutex_lock(&ledger.mutex);
    const bool resolved = module_resolve_symbol_locked(module, symbol_name, symbol_address);
    mutex_unlock(&ledger.mutex);
    return resolved;
}

void module_symbol_index_acquire() {
    mutex_lock(&ledger.mutex);
    if (ledger.symbol_index_users > 0) {
        ledger.symbol_index_users++;
        mutex_unlock(&ledger.mutex);
        return;
    }
    ledger.symbol_index_users = 1;

#ifdef ESP_PLATFORM
    if (memory_external_total() == 0) {
        // No PSRAM: the linear scan is good enough and internal RAM is too precious for the index.
        mutex_unlock(&ledger.mutex);
        return;
    }
#endif

    size_t total_words = 0;
    for (auto* module : ledger.modules) {
        size_t count = 0;
        size_t bucket_count = 0;
        if (module->internal != nullptr && module->internal->added
            && symbol_index_shape(module->symbols, count, bucket_count)) {
            total_words += bucket_count + count;
        }
    }

    auto* arena = total_words > 0
        ? static_cast<uint16_t*>(memory_alloc_with_policy(
              total_words * sizeof(uint16_t),
              &SYMBOL_INDEX_POLICY
          ))
        : nullptr;
    if (arena != nullptr) {
        ledger.symbol_index_arena = arena;
        uint16_t* cursor = arena;
        for (auto* module : ledger.modules) {
            size_t count = 0;
            size_t bucket_count = 0;
            if (module->internal != nullptr && module->internal->added
                && symbol_index_shape(module->symbols, count, bucket_count)) {
                build_symbol_index(module->internal->symbol_index, module->symbols, count, bucket_count, cursor);
            }
        }
    }
    mutex_unlock(&ledger.mutex);
}

void module_symbol_index_release() {
    mutex_lock(&ledger.mutex);
    if (ledger.symbol_index_users == 0) {
        mutex_unlock(&ledger.mutex);
        return;
    }
    ledger.symbol_index_users--;
    if (ledger.symbol_index_users == 0) {
        for (auto* module : ledger.modules) {
            if (module->internal != nullptr) {
                module->internal->symbol_index = {};
            }
        }
        memory_free(ledger.symbol_index_arena);
        ledger.symbol_index_arena = nullptr;
    }
    mutex_unlock(&ledger.mutex);
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
        if (module_resolve_symbol_locked(module, symbol_name, symbol_address)) {
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
