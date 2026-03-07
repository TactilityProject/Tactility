#include "Tactility/MountPoints.h"

#include "Tactility/TactilityConfig.h"
#include <tactility/hal/Device.h>

#include <Tactility/file/File.h>

#include <cstring>
#include <dirent.h>
#include <tactility/filesystem/file_system.h>
#include <vector>

namespace tt::file {

std::vector<dirent> getFileSystemDirents() {
    std::vector<dirent> dir_entries;

    file_system_for_each(&dir_entries, [](auto* fs, void* context) {
        if (!file_system_is_mounted(fs)) return true;
        char path[128];
        if (file_system_get_path(fs, path, sizeof(path)) != ERROR_NONE) return true;
        auto mount_name = std::string(path).substr(1);
        if (!config::SHOW_SYSTEM_PARTITION && mount_name.starts_with(SYSTEM_PARTITION_NAME)) return true;
        auto dir_entry = dirent {
            .d_ino = 2,
            .d_type = TT_DT_DIR,
            .d_name = { 0 }
        };
        auto& dir_entries = *static_cast<std::vector<dirent>*>(context);
        strcpy(dir_entry.d_name, mount_name.c_str());
        dir_entries.push_back(dir_entry);

        return true;
    });

    return dir_entries;
}

}
