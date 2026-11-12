// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <string>

// Resolves an AppManifestBinding::binary filename (without extension) to its installed path
// under this fixed, predictable location, mirroring what each platform's own loader
// (app_posix_loader_service.cpp / app_esp32_loader_service.cpp) already resolves a plain
// ".so"/".elf" AppLocation::location straight through unchanged - so setting location.location
// to this exact path means neither loader needs to guess which of a package's several binaries
// an AppManifest refers to.
inline std::string app_resolve_binary_path(const std::string& install_dir, const std::string& binary) {
#ifdef ESP_PLATFORM
    return install_dir + "/bin/" CONFIG_IDF_TARGET "/" + binary + ".elf";
#else
    return install_dir + "/bin/" TACTILITY_POSIX_PLATFORM "/" + binary + ".so";
#endif
}
