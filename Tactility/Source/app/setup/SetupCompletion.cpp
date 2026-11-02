#include <Tactility/app/setup/Setup.h>

#include <Tactility/file/File.h>

#include <tactility/log.h>
#include <tactility/paths.h>

// Included last, as it makes std:: resolve to tt::std
#include <Tactility/app/setup/SetupPrivate.h>

namespace tt::app::setup {

constexpr auto* TAG = "setup";

/** Increase when the setup changes, so users see it again */
constexpr int SETUP_VERSION = 3;

static bool getCompletedMarkerPath(::std::string& outPath) {
    char path[128];
    if (paths_get_data_path(path, sizeof(path)) != ERROR_NONE) {
        return false;
    }
    outPath = ::std::string(path) + "/.setup_complete";
    return true;
}

bool isCompleted() {
    ::std::string path;
    if (!getCompletedMarkerPath(path)) {
        LOG_E(TAG, "Setup path not found");
        return false;
    }
    if (!file::isFile(path)) {
        return false;
    }
    // Markers from before versioning are empty and mean version 1
    int version = 1;
    const auto content = file::readString(path);
    if (content != nullptr && content[0] != '\0') {
        version = atoi(reinterpret_cast<const char*>(content.get()));
    }
    return version >= SETUP_VERSION;
}

void markCompleted() {
    ::std::string path;
    if (!getCompletedMarkerPath(path)) {
        return;
    }
    if (!file::writeString(path, ::std::to_string(SETUP_VERSION))) {
        LOG_E(TAG, "Failed to save setup completion marker");
    }
}

}
