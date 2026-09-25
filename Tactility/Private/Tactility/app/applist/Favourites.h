#pragma once

#include <Tactility/file/File.h>

#include <app/paths.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace tt::app::applist {

/** Favourited app ids, persisted one per line under the data root. */
class Favourites final {

public:

    std::vector<std::string> load() const {
        std::vector<std::string> ids;
        const std::string path = filePath();
        if (!path.empty()) {
            file::readLines(path, /* stripNewLine */ true, [&ids](const char* line) {
                if (line[0] != '\0') {
                    ids.emplace_back(line);
                }
            });
        }
        return ids;
    }

    /** @return false if the data root is unavailable or the write itself failed - the previous
     * file, if any, is left untouched either way. */
    bool save(const std::vector<std::string>& ids) const {
        const std::string path = filePath();
        if (path.empty()) {
            return false;
        }
        file::findOrCreateParentDirectory(path, 0777);
        std::string content;
        for (const auto& id : ids) {
            content += id;
            content += '\n';
        }
        // Written to a temp file and renamed into place, so a write that fails partway through
        // can't truncate or corrupt the existing file.
        const std::string tempPath = path + ".tmp";
        if (!file::writeString(tempPath, content)) {
            remove(tempPath.c_str());
            return false;
        }
        // FatFs (ESP32's internal "/data" filesystem) rejects rename() onto an existing
        // destination instead of replacing it, unlike POSIX - remove it first so this works the
        // second and every later time, not just the first.
        remove(path.c_str());
        if (rename(tempPath.c_str(), path.c_str()) != 0) {
            remove(tempPath.c_str());
            return false;
        }
        return true;
    }

    static bool contains(const std::vector<std::string>& ids, const char* id) {
        return std::ranges::find(ids, id) != ids.end();
    }

    /** @return false if the new list failed to persist (see save()) - the caller should tell the
     * user, since the next load() will not reflect this toggle. */
    bool toggle(const char* id) const {
        auto ids = load();
        auto it = std::ranges::find(ids, id);
        if (it != ids.end()) {
            ids.erase(it);
        } else {
            ids.emplace_back(id);
        }
        return save(ids);
    }

private:

    static std::string filePath() {
        char path[128];
        if (app_paths_get_user_data_path("tactility.applist", "favourites", path, sizeof(path)) != ERROR_NONE) {
            return "";
        }
        return path;
    }
};

}
