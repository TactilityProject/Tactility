#pragma once

#include <Tactility/app/AppManifest.h>

#include <map>
#include <string>

namespace tt::app {

bool getValueFromManifest(const std::map<std::string, std::string>& map, const std::string& key, std::string& output);

bool isValidManifestVersion(const std::string& version);
bool isValidAppVersionName(const std::string& version);
bool isValidAppVersionCode(const std::string& version);
bool isValidName(const std::string& name);

/** Parses a V1 (sectioned INI, e.g. "[app]versionName=...") manifest map. */
bool parseManifestV1(const std::map<std::string, std::string>& map, AppManifest& manifest);

/** Parses a V2 (flat dot-notation, e.g. "app.version.name=...") manifest map. */
bool parseManifestV2(const std::map<std::string, std::string>& map, AppManifest& manifest);

}
