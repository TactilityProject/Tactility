// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/private/binary_path.h>

#include <TactilityCpp/Allocator.h>

#include <string>
#include <vector>

/**
 * Owns AppManifest copies (and their backing location-path strings) that app_manager's ledger
 * only keeps non-owning pointers to. Sized once, up front: AppManifest::location.location points
 * into the parallel `locations` vector, which would dangle if either vector reallocated after
 * construction - so neither vector is ever resized again once built.
 * OptExternalAllocator: this isn't on the app start/stop hot path, so prefer PSRAM for it, same
 * as AppLedger::packages and app_install()/app_manager_install_path_scan()'s own transient
 * AppManifestBinding vectors.
 */
struct AppManifestSet {
    std::vector<AppManifest, tt::OptExternalAllocator<AppManifest>> manifests;
    std::vector<std::string, tt::OptExternalAllocator<std::string>> locations;

    AppManifestSet(const std::string& install_dir, const AppManifestBinding* bindings, size_t count) {
        manifests.resize(count);
        locations.resize(count);
        for (size_t i = 0; i < count; i++) {
            manifests[i] = bindings[i].manifest;
            locations[i] = app_resolve_binary_path(install_dir, bindings[i].binary);
            manifests[i].location = { APP_LOCATION_PATH, const_cast<char*>(locations[i].c_str()) };
        }
    }
};
