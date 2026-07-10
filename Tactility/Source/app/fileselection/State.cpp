#include "Tactility/app/fileselection/State.h"

#include <Tactility/file/File.h>
#include <Tactility/MountPoints.h>
#include <Tactility/Platform.h>

#include <tactility/log.h>

#include <dirent.h>
#include <unistd.h>
#include <vector>

namespace tt::app::fileselection {

constexpr auto* TAG = "FileSelection";

State::State() {
    if (kernel::getPlatform() == kernel::PlatformSimulator) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            setEntriesForPath(cwd);
        } else {
            LOG_E(TAG, "Failed to get current work directory files");
            setEntriesForPath("/");
        }
    } else {
        setEntriesForPath("/");
    }
}

std::string State::getSelectedChildPath() const {
    return file::getChildPath(current_path, selected_child_entry);
}

bool State::setEntriesForPath(const std::string& path) {
    LOG_I(TAG, "Changing path: %s -> %s", current_path.c_str(), path.c_str());

    auto lock = mutex.asScopedLock();
    if (!lock.lock(100)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "setEntriesForPath");
        return false;
    }

    /**
     * ESP32 does not have a root directory, so we have to create it manually.
     * We'll add the NVS Flash partitions and the binding for the sdcard.
     */
    bool show_custom_root = (kernel::getPlatform() == kernel::PlatformEsp) && (path == "/");
    if (show_custom_root) {
        LOG_I(TAG, "Setting custom root");
        dir_entries = file::getFileSystemDirents();
        current_path = path;
        selected_child_entry = "";
        return true;
    } else {
        dir_entries.clear();
        int count = file::scandir(path, dir_entries, &file::direntFilterDotEntries, file::direntSortAlphaAndType);
        if (count >= 0) {
            LOG_I(TAG, "%s has %d entries", path.c_str(), count);
            current_path = path;
            selected_child_entry = "";
            return true;
        } else {
            LOG_E(TAG, "Failed to fetch entries for %s", path.c_str());
            return false;
        }
    }
}

bool State::setEntriesForChildPath(const std::string& childPath) {
    auto path = file::getChildPath(current_path, childPath);
    LOG_I(TAG, "Navigating from %s to %s", current_path.c_str(), path.c_str());
    return setEntriesForPath(path);
}

bool State::getDirent(uint32_t index, dirent& dirent) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        return false;
    }

    if (index < dir_entries.size()) {
        dirent = dir_entries[index];
        return true;
    } else {
        return false;
    }
}

}
