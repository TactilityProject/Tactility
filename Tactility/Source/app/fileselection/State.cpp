#include "Tactility/app/fileselection/State.h"

#include <Tactility/file/File.h>
#include <Tactility/MountPoints.h>

#include <tactility/log.h>

#include <dirent.h>
#include <unistd.h>
#include <vector>

namespace tt::app::fileselection {

constexpr auto* TAG = "FileSelection";

State::State() {
    setEntriesForPath("/");
}

std::string State::getSelectedChildPath() const {
    return file::getChildPath(current_path, selected_child_entry);
}

bool State::setEntriesForPath(const std::string& path) {
    LOG_D(TAG, "Changing path: %s -> %s", current_path.c_str(), path.c_str());

    auto lock = mutex.asScopedLock();
    if (!lock.lock(100)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "setEntriesForPath");
        return false;
    }

    dir_entries.clear();
    int count = file::scandir(path, dir_entries, &file::direntFilterDotEntries, file::direntSortAlphaAndType);
    if (count >= 0) {
        current_path = path;
        selected_child_entry = "";
        return true;
    } else {
        LOG_E(TAG, "Failed to fetch entries for %s", path.c_str());
        return false;
    }
}

bool State::setEntriesForChildPath(const std::string& childPath) {
    auto path = file::getChildPath(current_path, childPath);
    LOG_D(TAG, "Navigating from %s to %s", current_path.c_str(), path.c_str());
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
