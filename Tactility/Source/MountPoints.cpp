#include "Tactility/MountPoints.h"

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
        // ESP32 mount paths are short names ("/data"); POSIX file systems can return a full
        // absolute host path instead, so take the last path component either way.
        auto path_str = std::string(path);
        auto slash_pos = path_str.find_last_of('/');
        auto mount_name = slash_pos == std::string::npos ? path_str : path_str.substr(slash_pos + 1);
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
