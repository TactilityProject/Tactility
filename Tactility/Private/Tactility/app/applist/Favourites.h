#pragma once

#include <Tactility/file/File.h>

#include <app/paths.h>

#include <algorithm>
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

    void save(const std::vector<std::string>& ids) const {
        const std::string path = filePath();
        if (path.empty()) {
            return;
        }
        file::findOrCreateParentDirectory(path, 0777);
        std::string content;
        for (const auto& id : ids) {
            content += id;
            content += '\n';
        }
        file::writeString(path, content);
    }

    static bool contains(const std::vector<std::string>& ids, const char* id) {
        return std::ranges::find(ids, id) != ids.end();
    }

    void toggle(const char* id) const {
        auto ids = load();
        auto it = std::ranges::find(ids, id);
        if (it != ids.end()) {
            ids.erase(it);
        } else {
            ids.emplace_back(id);
        }
        save(ids);
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
