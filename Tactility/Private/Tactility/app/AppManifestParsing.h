#pragma once

#include <Tactility/app/AppManifest.h>

#include <string>

namespace tt::app {

bool isValidId(const std::string& id);

/** Parses a manifest.properties file, auto-detecting the V1 (sectioned) or V2 (flat) format from its first line. */
bool parseManifest(const std::string& filePath, AppManifest& manifest);

}
