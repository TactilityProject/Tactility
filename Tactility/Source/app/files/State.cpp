#include <Tactility/app/files/State.h>

#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <tactility/log.h>

#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <vector>

namespace tt::app::files {

constexpr auto* TAG = "Files";

State::State() {
    setEntriesForPath("/");
}

std::string State::getSelectedChildPath() const {
    return file::getChildPath(current_path, selected_child_entry);
}

bool State::setEntriesForPath(const std::string& path) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(100)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "setEntriesForPath");
        return false;
    }

    LOG_I(TAG, "Changing path: %s -> %s", current_path.c_str(), path.c_str());

    dir_entries.clear();
    int count = file::scandir(path, dir_entries, nullptr, file::direntSortAlphaAndType);
    if (count == 0) {
        LOG_E(TAG, "Failed to fetch entries for %s", path.c_str());
        return false;
    }

    std::erase_if(dir_entries, [](const dirent& entry) {
        return strcmp(".", entry.d_name) == 0 || strcmp("..", entry.d_name) == 0;
    });

    LOG_I(TAG, "%s has %d entries", path.c_str(), dir_entries.size());
    current_path = path;
    selected_child_entry = "";
    action = ActionNone;
    return true;
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
