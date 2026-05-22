#pragma once

#include <string>

namespace tt::app::files {

bool isSupportedAppFile(const std::string& filename);
bool isSupportedImageFile(const std::string& filename);
bool isSupportedTextFile(const std::string& filename);
bool isSupportedAudioFile(const std::string& filename);

} // namespace
