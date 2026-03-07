#include "Tactility/app/fileselection/State.h"

#include <Tactility/file/File.h>
#include "Tactility/hal/sdcard/SdCardDevice.h"
#include <Tactility/Logger.h>
#include <Tactility/MountPoints.h>
#include <Tactility/kernel/Platform.h>

#include <Tactility/LogMessages.h>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <vector>

namespace tt::app::fileselection {

static const auto LOGGER = Logger("FileSelection");

State::State() {
    if (kernel::getPlatform() == kernel::PlatformSimulator) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            setEntriesForPath(cwd);
        } else {
            LOGGER.error("Failed to get current work directory files");
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
    LOGGER.info("Changing path: {} -> {}", current_path, path);

    auto lock = mutex.asScopedLock();
    if (!lock.lock(100)) {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "setEntriesForPath");
        return false;
    }

    /**
     * ESP32 does not have a root directory, so we have to create it manually.
     * We'll add the NVS Flash partitions and the binding for the sdcard.
     */
    bool show_custom_root = (kernel::getPlatform() == kernel::PlatformEsp) && (path == "/");
    if (show_custom_root) {
        LOGGER.info("Setting custom root");
        dir_entries = file::getFileSystemDirents();
        current_path = path;
        selected_child_entry = "";
        return true;
    } else {
        dir_entries.clear();
        int count = file::scandir(path, dir_entries, &file::direntFilterDotEntries, file::direntSortAlphaAndType);
        if (count >= 0) {
            LOGGER.info("{} has {} entries", path, count);
            current_path = path;
            selected_child_entry = "";
            return true;
        } else {
            LOGGER.error("Failed to fetch entries for {}", path);
            return false;
        }
    }
}

bool State::setEntriesForChildPath(const std::string& childPath) {
    auto path = file::getChildPath(current_path, childPath);
    LOGGER.info("Navigating from {} to {}", current_path, path);
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
