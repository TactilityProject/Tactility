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
    LOG_D(TAG, "Changing path: %s", path.c_str());

    std::vector<dirent> entries;
    if (file::scandir(path, entries, &file::direntFilterDotEntries, file::direntSortAlphaAndType) < 0) {
        LOG_E(TAG, "Failed to fetch entries for %s", path.c_str());
        return false;
    }

    auto lock = mutex.asScopedLock();
    if (!lock.lock(100)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "setEntriesForPath");
        return false;
    }

    dir_entries = std::move(entries);
    current_path = path;
    selected_child_entry = "";
    return true;
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
