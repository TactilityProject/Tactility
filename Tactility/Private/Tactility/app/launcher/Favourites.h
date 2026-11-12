#pragma once

#include <Tactility/file/File.h>

#include <tactility/Memory.hpp>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <app/paths.h>
#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>

#include <algorithm>
#include <cstdio>

namespace tt::app::launcher {

/**
 * Favourited app ids, persisted one per line under the data root.
 * The ids are cached in memory: ids(), isFavourite() and toggle() never touch files and can be used from any task.
 * load() and save() do file operations, so they must run on the file operations task, see <tactility/file_ops.h>.
 */
class Favourites final {

    struct State {
        Mutex mutex;
        std::vector<std::string> ids;
        /** Toggling before the first load() would save a list without the favourites in the file */
        bool loaded = false;

        State() { mutex_construct(&mutex); }
        ~State() { mutex_destruct(&mutex); }
    };

    static State& state() {
        static State* instance = tt::newObject<State>();
        check(instance != nullptr);
        return *instance;
    }

public:

    /** Replaces the cached ids with the ones in the file. File operation. */
    void load() const {
        std::vector<std::string> loaded;
        const ::std::string path = filePath();
        if (!path.empty()) {
            file::readLines(path, /* stripNewLine */ true, [&loaded](const char* line) {
                if (line[0] != '\0') {
                    loaded.emplace_back(line);
                }
            });
        }
        auto& s = state();
        mutex_lock(&s.mutex);
        s.ids = ::std::move(loaded);
        s.loaded = true;
        mutex_unlock(&s.mutex);
    }

    /** @return a copy of the cached ids */
    std::vector<std::string> ids() const {
        auto& s = state();
        mutex_lock(&s.mutex);
        std::vector<std::string> result(s.ids.begin(), s.ids.end());
        mutex_unlock(&s.mutex);
        return result;
    }

    /** Adds the id to the cached ids when it's not in there yet, otherwise removes it.
     * @param[out] result the new cached ids, to be passed to save()
     * @return false when load() hasn't completed yet, in which case nothing changed */
    bool toggle(const char* id, std::vector<std::string>& result) const {
        auto& s = state();
        mutex_lock(&s.mutex);
        if (!s.loaded) {
            mutex_unlock(&s.mutex);
            return false;
        }
        auto it = ::std::ranges::find(s.ids, id);
        if (it != s.ids.end()) {
            s.ids.erase(it);
        } else {
            s.ids.emplace_back(id);
        }
        result.assign(s.ids.begin(), s.ids.end());
        mutex_unlock(&s.mutex);
        return true;
    }

    /** Writes the ids to the file. File operation.
     * @return false if the data root is unavailable or the write itself failed. The previous
     * file, if any, is left untouched either way. */
    bool save(const std::vector<std::string>& ids) const {
        const ::std::string path = filePath();
        if (path.empty()) {
            return false;
        }
        file::findOrCreateParentDirectory(path, 0777);
        ::std::string content;
        for (const auto& id : ids) {
            content += id;
            content += '\n';
        }
        // Written to a temp file and renamed into place, so a write that fails partway through
        // can't truncate or corrupt the existing file.
        const ::std::string tempPath = path + ".tmp";
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
        return ::std::ranges::find(ids, id) != ids.end();
    }

private:

    static ::std::string filePath() {
        char path[128];
        if (app_paths_get_user_data_path("tactility.launcher", "favourites", path, sizeof(path)) != ERROR_NONE) {
            return "";
        }
        return path;
    }
};

}
