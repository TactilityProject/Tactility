#pragma once

#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/RecursiveMutex.h>

#include <algorithm>
#include <string>
#include <vector>
#include <dirent.h>

// Included last, as it makes std:: resolve to tt::std
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

namespace tt::app::fileselection {

class State final {

    RecursiveMutex mutex;
    Options options;
    ::std::string current_path = "/";
    /** The allowed extensions without dot and in lower case */
    tt::std::vector<tt::std::string> extensions;
    tt::std::vector<tt::std::string> selected_paths;
    bool loaded = false;

public:

    explicit State(Options options);

    static tt::std::string toExt(::std::string_view text) {
        return tt::std::string(text.data(), text.size());
    }

    static tt::std::vector<tt::std::string> toExt(const ::std::vector<::std::string>& texts) {
        tt::std::vector<tt::std::string> result;
        result.reserve(texts.size());
        for (const auto& text : texts) {
            result.push_back(toExt(text));
        }
        return result;
    }

    static ::std::vector<::std::string> toStd(const tt::std::vector<tt::std::string>& texts) {
        ::std::vector<::std::string> result;
        result.reserve(texts.size());
        for (const auto& text : texts) {
            result.emplace_back(text.data(), text.size());
        }
        return result;
    }

    /** @return the index of the text, or -1 */
    static ptrdiff_t indexOf(const tt::std::vector<tt::std::string>& texts, ::std::string_view text) {
        for (size_t i = 0; i < texts.size(); i++) {
            if (::std::string_view(texts[i]) == text) {
                return static_cast<ptrdiff_t>(i);
            }
        }
        return -1;
    }


    const Options& getOptions() const { return options; }

    /** Whether a file path is allowed by the options (files allowed and extension matches) */
    bool isSelectableFile(const ::std::string& path) const;

    /** Adds the path to the selected files, or removes it when it was selected. Returns whether it is selected now. */
    bool toggleSelectedPath(const ::std::string& path);

    bool isSelectedPath(const ::std::string& path) const {
        bool result = false;
        mutex.withLock([&] { result = indexOf(selected_paths, path) >= 0; });
        return result;
    }

    ::std::vector<::std::string> getSelectedPaths() const {
        ::std::vector<::std::string> result;
        mutex.withLock([&] { result = toStd(selected_paths); });
        return result;
    }

    /** Replaces the selected files */
    void setSelectedPaths(const ::std::vector<::std::string>& paths) {
        mutex.withLock([&] { selected_paths = toExt(paths); });
    }

    /** Sets the directory that is shown, after it was read */
    void setCurrentPath(const ::std::string& path) {
        mutex.withLock([&] {
            current_path = path;
            loaded = true;
        });
    }

    /** Whether a directory was read since the app started */
    bool isLoaded() const {
        bool result = false;
        mutex.withLock([&] { result = loaded; });
        return result;
    }

    ::std::string getCurrentPath() const {
        ::std::string result;
        mutex.withLock([&] { result = current_path; });
        return result;
    }
};

}
