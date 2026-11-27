// SPDX-License-Identifier: Apache-2.0
#include <tactility/firmware/firmware.h>

#include <tactility/Memory.hpp>
#include <tactility/concurrent/mutex.h>
#include <tactility/std/vector.hpp>

#include <cstring>

namespace {

struct FirmwareEntry {
    const char* id;
    const FirmwareOps* ops;
    void* ctx;
};

using FirmwareList = tt::std::vector<FirmwareEntry>;

struct RegistryMutex {
    Mutex handle {};
    RegistryMutex() { mutex_construct(&handle); }
    ~RegistryMutex() { mutex_destruct(&handle); }
};

RegistryMutex registryMutex;

/** Allocated on first registration and never freed. Guarded by registryMutex. */
FirmwareList* entries = nullptr;

FirmwareEntry* find(const char* id) {
    if (entries == nullptr) {
        return nullptr;
    }
    for (auto& entry : *entries) {
        if (strcmp(entry.id, id) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

extern "C" {

error_t firmware_register(const char* id, const FirmwareOps* ops, void* ctx) {
    mutex_lock(&registryMutex.handle);
    error_t result = ERROR_NONE;
    if (find(id) != nullptr) {
        result = ERROR_ALREADY_EXISTS;
    } else {
        if (entries == nullptr) {
            entries = tt::newObject<FirmwareList>();
        }
        if (entries == nullptr) {
            result = ERROR_OUT_OF_MEMORY;
        } else {
            entries->push_back({ id, ops, ctx });
        }
    }
    mutex_unlock(&registryMutex.handle);
    return result;
}

error_t firmware_unregister(const char* id) {
    mutex_lock(&registryMutex.handle);
    auto* entry = find(id);
    if (entry != nullptr) {
        entries->erase(entries->begin() + (entry - entries->data()));
    }
    mutex_unlock(&registryMutex.handle);
    return entry != nullptr ? ERROR_NONE : ERROR_NOT_FOUND;
}

error_t firmware_get(const char* id, const FirmwareOps** ops, void** ctx) {
    mutex_lock(&registryMutex.handle);
    const auto* entry = find(id);
    if (entry != nullptr) {
        *ops = entry->ops;
        *ctx = entry->ctx;
    }
    mutex_unlock(&registryMutex.handle);
    return entry != nullptr ? ERROR_NONE : ERROR_NOT_FOUND;
}

void firmware_for_each(FirmwareVisitorFn visitor, void* user_data) {
    mutex_lock(&registryMutex.handle);
    if (entries != nullptr) {
        for (const auto& entry : *entries) {
            if (!visitor(entry.id, entry.ops, entry.ctx, user_data)) {
                break;
            }
        }
    }
    mutex_unlock(&registryMutex.handle);
}

}
