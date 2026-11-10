#include <Tactility/app/files/State.h>

#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <tactility/log.h>
#include <tactility/paths.h>

#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace tt::app::files {

constexpr auto* TAG = "Files";

State::State() {
    char data_root[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_root_path(data_root, sizeof(data_root)) != ERROR_NONE || !setEntriesForPath(data_root)) {
        setEntriesForPath("/");
    }
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

    std::vector<dirent> entries;
    if (file::scandir(path, entries, nullptr, file::direntSortAlphaAndType) < 0) {
        LOG_E(TAG, "Failed to fetch entries for %s", path.c_str());
        return false;
    }

    std::erase_if(entries, [](const dirent& entry) {
        return strcmp(".", entry.d_name) == 0 || strcmp("..", entry.d_name) == 0;
    });

    LOG_I(TAG, "%s has %d entries", path.c_str(), entries.size());
    dir_entries = std::move(entries);
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

}
