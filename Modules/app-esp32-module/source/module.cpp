// SPDX-License-Identifier: Apache-2.0
#include <app_esp32/module.h>

#include <private/elf_symbol.h>

#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

extern ServiceManifest loader_service_manifest;
extern const ModuleSymbol app_esp32_symbols[];
extern Module app_esp32_module;

// Overrides elf_loader's default KConfig-based symbol resolver with one that looks up symbols
// across every started kernel module's own symbol table instead. This module's own symbols come
// first, as they replace libc functions for apps (see app_symbols.cpp).
uintptr_t app_esp32_symbol_resolver(const char* symbolName) {
    uintptr_t symbol_address;
    if (module_resolve_symbol(&app_esp32_module, symbolName, &symbol_address) ||
        module_resolve_symbol_global(symbolName, &symbol_address)) {
        return symbol_address;
    }
    return 0;
}

static error_t start() {
    elf_set_symbol_resolver(app_esp32_symbol_resolver);
    return service_manager_add(&loader_service_manifest, /*auto_start=*/true);
}

static error_t stop() {
    elf_set_symbol_resolver(nullptr);
    return service_manager_remove(loader_service_manifest.id);
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
