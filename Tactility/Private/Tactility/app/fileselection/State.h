#pragma once

#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/RecursiveMutex.h>

#include <algorithm>
#include <string>
#include <vector>
#include <dirent.h>

namespace tt::app::fileselection {

class State final {

    RecursiveMutex mutex;
    Options options;
    std::vector<dirent> dir_entries;
    std::string current_path;
    std::string selected_child_entry;
    std::vector<std::string> selected_paths;
    uint32_t completed_navigations = 0;

public:

    explicit State(Options options);

    const Options& getOptions() const { return options; }

    /** Whether a file path is allowed by the options (files allowed and extension matches) */
    bool isSelectableFile(const std::string& path) const;

    /** Adds the path to the selected files, or removes it when it was selected. Returns whether it is selected now. */
    bool toggleSelectedPath(const std::string& path);

    bool isSelectedPath(const std::string& path) const {
        bool result = false;
        mutex.withLock([&] { result = std::ranges::find(selected_paths, path) != selected_paths.end(); });
        return result;
    }

    std::vector<std::string> getSelectedPaths() const {
        std::vector<std::string> result;
        mutex.withLock([&] { result = selected_paths; });
        return result;
    }

    void freeEntries() {
        dir_entries.clear();
    }

    ~State() {
        freeEntries();
    }

    /** Reads the directory and replaces the entries. Does file operations, so it must not run on the LVGL task. */
    bool setEntriesForPath(const std::string& path);

    /** Records that a queued navigation succeeded */
    void pushCompletedNavigation() {
        mutex.withLock([&] { completed_navigations++; });
    }

    /** Consumes one succeeded navigation, or returns false when there is none */
    bool takeCompletedNavigation() {
        bool result = false;
        mutex.withLock([&] {
            if (completed_navigations > 0) {
                completed_navigations--;
                result = true;
            }
        });
        return result;
    }

    template <std::invocable<const std::vector<dirent> &> Func>
    void withEntries(Func&& onEntries) const {
        mutex.withLock([&]() {
            std::invoke(std::forward<Func>(onEntries), dir_entries);
        });
    }

    bool getDirent(uint32_t index, dirent& dirent);

    void setSelectedChildEntry(const std::string& newFile) {
        mutex.withLock([&] { selected_child_entry = newFile; });
    }

    std::string getSelectedChildEntry() const {
        std::string result;
        mutex.withLock([&] { result = selected_child_entry; });
        return result;
    }

    std::string getCurrentPath() const {
        std::string result;
        mutex.withLock([&] { result = current_path; });
        return result;
    }

    std::string getCurrentPathWithTrailingSlash() const {
        std::string path = getCurrentPath();
        return path.length() > 1 ? path + "/" : path;
    }

    std::string getSelectedChildPath() const;
};

}
