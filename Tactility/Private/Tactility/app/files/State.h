#pragma once

#include <Tactility/RecursiveMutex.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <dirent.h>

namespace tt::app::files {

class State final {

public:

    enum PendingAction {
        ActionNone,
        ActionDelete,
        ActionRename,
        ActionCreateFile,
        ActionCreateFolder,
        ActionPaste
    };

private:

    RecursiveMutex mutex;
    std::vector<dirent> dir_entries;
    std::string current_path;
    std::string selected_child_entry;
    PendingAction action = ActionNone;
    std::string pending_paste_dst;
    std::string clipboard_path;
    bool clipboard_is_cut = false;
    bool clipboard_active = false;

public:


    State();

    void freeEntries() {
        dir_entries.clear();
    }

    ~State() {
        freeEntries();
    }

    bool setEntriesForChildPath(const std::string& child_path);
    bool setEntriesForPath(const std::string& path);

    template <std::invocable<const std::vector<dirent> &> Func>
    void withEntries(Func&& onEntries) const {
        mutex.withLock([&] {
            std::invoke(std::forward<Func>(onEntries), dir_entries);
        });
    }

    bool getDirent(uint32_t index, dirent& dirent);

    void setSelectedChildEntry(const std::string& newFile) {
        selected_child_entry = newFile;
        action = ActionNone;
    }

    std::string getSelectedChildEntry() const { return selected_child_entry; }
    std::string getCurrentPath() const { return current_path; }

    std::string getSelectedChildPath() const;

    PendingAction getPendingAction() const { return action; }

    void setPendingAction(PendingAction newAction) { action = newAction; }

    // These accessors intentionally omit mutex locking: both are only called
    // from the UI thread (onPastePressed → onResult), so no concurrent access
    // is possible.  If that threading assumption changes, add mutex guards here
    // to match the clipboard accessors above.
    std::string getPendingPasteDst() const { return pending_paste_dst; }
    void setPendingPasteDst(const std::string& dst) { pending_paste_dst = dst; }

    void setClipboard(const std::string& path, bool is_cut) {
        mutex.withLock([&] {
            clipboard_path = path;
            clipboard_is_cut = is_cut;
            clipboard_active = true;
        });
    }

    bool hasClipboard() const {
        bool result = false;
        mutex.withLock([&] { result = clipboard_active; });
        return result;
    }

    /** Returns {path, is_cut} atomically, or nullopt if clipboard is empty. */
    std::optional<std::pair<std::string, bool>> getClipboard() const {
        std::optional<std::pair<std::string, bool>> result;
        mutex.withLock([&] {
            if (clipboard_active) {
                result = { clipboard_path, clipboard_is_cut };
            }
        });
        return result;
    }

    void clearClipboard() {
        mutex.withLock([&] {
            clipboard_active = false;
            clipboard_path.clear();
            clipboard_is_cut = false;
        });
    }
};

}
