#pragma once

#include <Tactility/RecursiveMutex.h>

#include <string>
#include <vector>
#include <dirent.h>

namespace tt::app::fileselection {

class State final {

    RecursiveMutex mutex;
    std::vector<dirent> dir_entries;
    std::string current_path;
    std::string selected_child_entry;
    uint32_t completed_navigations = 0;

public:

    State();

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
