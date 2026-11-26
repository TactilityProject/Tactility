#include <Tactility/file/File.h>

#include <tactility/log.h>
#include <tactility/paths.h>

#include <Tactility/StringUtils.h>

#include <algorithm>
#include <dirent.h>
#include <unistd.h>
#include <vector>

// Included last, as it makes std:: resolve to tt::std
#include "Tactility/app/fileselection/State.h"

namespace tt::app::fileselection {

namespace {

/** Resolves a path against the working directory, without "." and ".." segments */
::std::string toAbsolutePath(const ::std::string& path) {
    char working_directory[FILE_MAX_PATH_STRING_LENGTH];
    const ::std::string base = getcwd(working_directory, sizeof(working_directory)) != nullptr ? working_directory : "/";
    const ::std::string joined = base + "/" + path;
    ::std::string result;
    size_t start = 0;
    while (start < joined.size()) {
        size_t end = joined.find('/', start);
        if (end == ::std::string::npos) {
            end = joined.size();
        }
        const auto segment = joined.substr(start, end - start);
        if (segment == "..") {
            result.erase(result.empty() ? 0 : result.find_last_of('/'));
        } else if (!segment.empty() && segment != ".") {
            result += "/" + segment;
        }
        start = end + 1;
    }
    return result.empty() ? "/" : result;
}

}

State::State(Options options) : options(::std::move(options)) {
    // The view falls back to the root when the directory can't be read
    if (!this->options.initialPath.empty()) {
        current_path = this->options.initialPath;
        if (current_path.front() != '/') {
            current_path = toAbsolutePath(current_path);
        }
    } else {
        char data_root[FILE_MAX_PATH_STRING_LENGTH];
        current_path = paths_get_root_path(data_root, sizeof(data_root)) == ERROR_NONE ? data_root : "/";
    }
    for (const auto& extension : this->options.extensions) {
        auto text = ::std::string_view(extension);
        if (!text.empty() && text[0] == '.') {
            text.remove_prefix(1);
        }
        extensions.push_back(toExt(string::lowercase(::std::string(text))));
    }
    // The extensions are kept in external RAM
    this->options.extensions = {};
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

}
