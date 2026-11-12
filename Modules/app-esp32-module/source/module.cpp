// SPDX-License-Identifier: Apache-2.0
#include <app_esp32/module.h>

#include "module_refs.h"

#include <private/elf_symbol.h>

#include <service/manager.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

extern ServiceManifest loader_service_manifest;
extern const ModuleSymbol app_esp32_symbols[];
extern Module app_esp32_module;

} // extern "C"

namespace {

// One ELF is relocated at a time, so the resolver can attribute the modules it hands out
Mutex relocation_mutex {};
ModuleRefs* current_refs = nullptr;

}

void module_refs_construct() {
    mutex_construct(&relocation_mutex);
}

void module_refs_destruct() {
    mutex_destruct(&relocation_mutex);
}

void module_refs_begin(ModuleRefs* refs) {
    mutex_lock(&relocation_mutex);
    current_refs = refs;
}

void module_refs_end() {
    current_refs = nullptr;
    mutex_unlock(&relocation_mutex);
}

void module_refs_release(ModuleRefs* refs) {
    for (auto* module : refs->modules) {
        module_put_code(module);
    }
    refs->modules.clear();
}

extern "C" {

// Overrides elf_loader's default KConfig-based symbol resolver with one that looks up symbols
// across every started kernel module's own symbol table instead. This module's own symbols come
// first, as they replace libc functions for apps (see app_symbols.cpp).
// While an ELF is relocated, the modules it resolves symbols from are referenced, so that their
// code stays mapped while it runs. It's a code reference, so it doesn't stop an app from stopping
// such a module on purpose (e.g. to take over the display) and starting it again afterwards.
uintptr_t app_esp32_symbol_resolver(const char* symbolName) {
    uintptr_t symbol_address;
    if (module_resolve_symbol(&app_esp32_module, symbolName, &symbol_address)) {
        return symbol_address;
    }

    if (current_refs == nullptr) {
        return module_resolve_symbol_global(symbolName, &symbol_address) ? symbol_address : 0;
    }

    Module* owner = nullptr;
    if (!module_resolve_symbol_global_get_code(symbolName, &symbol_address, &owner)) {
        return 0;
    }
    // One reference per module is enough
    for (auto* module : current_refs->modules) {
        if (module == owner) {
            module_put_code(owner);
            return symbol_address;
        }
    }
    current_refs->modules.push_back(owner);
    return symbol_address;
}

static error_t start() {
    module_refs_construct();
    elf_set_symbol_resolver(app_esp32_symbol_resolver);
    return service_manager_add(&loader_service_manifest, /*auto_start=*/true);
}

static error_t stop() {
    elf_set_symbol_resolver(nullptr);
    error_t error = service_manager_remove(loader_service_manifest.id);
    module_refs_destruct();
    return error;
}

Module app_esp32_module = {
    .name = "app-esp32",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = app_esp32_symbols,
    .internal = nullptr,
};

}
