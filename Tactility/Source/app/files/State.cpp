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

// Included last, as it makes std:: resolve to tt::std
#include <Tactility/app/files/State.h>

namespace tt::app::files {

constexpr auto* TAG = "Files";

State::State() {
    // The directory is read by the view, which falls back to the root when it can't be read
    char data_root[FILE_MAX_PATH_STRING_LENGTH];
    current_path = paths_get_root_path(data_root, sizeof(data_root)) == ERROR_NONE ? data_root : "/";
}

::std::string State::getSelectedChildPath() const {
    ::std::string path;
    ::std::string child;
    mutex.withLock([&] {
        path = current_path;
        child = selected_child_entry;
    });
    return file::getChildPath(path, child);
}

}
