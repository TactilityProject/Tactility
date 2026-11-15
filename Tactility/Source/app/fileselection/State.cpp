#include <Tactility/file/File.h>
#include <Tactility/MountPoints.h>

#include <tactility/log.h>

#include <Tactility/StringUtils.h>

#include <algorithm>
#include <dirent.h>
#include <unistd.h>
#include <vector>

// Included last, as it makes std:: resolve to tt::std
#include "Tactility/app/fileselection/State.h"

namespace tt::app::fileselection {

constexpr auto* TAG = "FileSelection";

State::State(Options options) : options(::std::move(options)) {
    for (const auto& extension : this->options.extensions) {
        auto text = ::std::string_view(extension);
        if (!text.empty() && text[0] == '.') {
            text.remove_prefix(1);
        }
        extensions.push_back(toExt(string::lowercase(::std::string(text))));
    }
    // The extensions are kept in external RAM
    this->options.extensions = {};
    setEntriesForPath("/");
}

bool State::isSelectableFile(const ::std::string& path) const {
    if (!options.allowFiles) {
        return false;
    }
    if (extensions.empty()) {
        return true;
    }
    auto name_start = path.find_last_of('/');
    auto dot = path.find_last_of('.');
    if (dot == ::std::string::npos || (name_start != ::std::string::npos && dot < name_start)) {
        return false;
    }
    auto extension = string::lowercase(path.substr(dot + 1));
    return indexOf(extensions, extension) >= 0;
}

bool State::toggleSelectedPath(const ::std::string& path) {
    bool selected = false;
    mutex.withLock([&] {
        const auto index = indexOf(selected_paths, path);
        if (index >= 0) {
            selected_paths.erase(selected_paths.begin() + index);
        } else {
            selected_paths.push_back(toExt(path));
            selected = true;
        }
    });
    return selected;
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

bool State::setEntriesForPath(const ::std::string& path) {
    LOG_D(TAG, "Changing path: %s", path.c_str());

    ::std::vector<dirent> entries;
    if (file::scandir(path, entries, &file::direntFilterDotEntries, file::direntSortAlphaAndType) < 0) {
        LOG_E(TAG, "Failed to fetch entries for %s", path.c_str());
        return false;
    }

    ::std::erase_if(entries, [this](const dirent& entry) {
        if (entry.d_type == file::TT_DT_DIR || entry.d_type == file::TT_DT_CHR) {
            return false;
        }
        return !isSelectableFile(entry.d_name);
    });

    auto lock = mutex.asScopedLock();
    if (!lock.lock(100)) {
        LOG_E(TAG, "Mutex acquisition timeout (%s)", "setEntriesForPath");
        return false;
    }

    dir_entries = ::std::move(entries);
    current_path = path;
    selected_child_entry = "";
    return true;
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
