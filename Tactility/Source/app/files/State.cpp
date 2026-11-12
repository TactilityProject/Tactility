#include <Tactility/app/files/State.h>

#include <Tactility/app/files/SupportedFiles.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <app/execute.h>
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
    std::string path;
    std::string child;
    mutex.withLock([&] {
        path = current_path;
        child = selected_child_entry;
    });
    return file::getChildPath(path, child);
}

bool State::setEntriesForPath(const std::string& path) {
    LOG_I(TAG, "Changing path: %s", path.c_str());

    std::vector<dirent> entries;
    if (file::scandir(path, entries, nullptr, file::direntSortAlphaAndType) < 0) {
        LOG_E(TAG, "Failed to fetch entries for %s", path.c_str());
        return false;
    }

    std::erase_if(entries, [](const dirent& entry) {
        return strcmp(".", entry.d_name) == 0 || strcmp("..", entry.d_name) == 0;
    });

    std::vector<std::string> executables;
    for (const auto& entry : entries) {
        const bool is_plain_file = entry.d_type != file::TT_DT_DIR && entry.d_type != file::TT_DT_CHR && entry.d_type != file::TT_DT_LNK;
        if (is_plain_file && !isSupportedImageFile(entry.d_name) && !isSupportedTextFile(entry.d_name) && !isSupportedAppFile(entry.d_name)) {
            const std::string child_path = file::getChildPath(path, entry.d_name);
            AppLocation location { APP_LOCATION_PATH, const_cast<char*>(child_path.c_str()) };
            if (app_is_executable(location)) {
                executables.push_back(entry.d_name);
            }
        }
    }

    auto lock = mutex.asScopedLock();
    if (!lock.lock(100)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "setEntriesForPath");
        return false;
    }

    LOG_I(TAG, "%s has %d entries", path.c_str(), entries.size());
    dir_entries = std::move(entries);
    executable_names = std::move(executables);
    current_path = path;
    selected_child_entry = "";
    action = ActionNone;
    return true;
}

}
