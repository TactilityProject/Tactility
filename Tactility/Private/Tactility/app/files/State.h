#pragma once

#include <Tactility/RecursiveMutex.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

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
    std::vector<std::string> executable_names;
    std::vector<std::pair<bool, bool>> completed_navigations;
    std::string current_path;
    std::string selected_child_entry;
    PendingAction action = ActionNone;
    std::string pending_paste_dst;
    std::string pending_paste_src;
    bool pending_paste_is_cut = false;
    struct stat pending_paste_dst_stat {};
    bool pending_paste_dst_stat_valid = false;
    std::string clipboard_path;
    bool clipboard_is_cut = false;
    bool clipboard_active = false;
    std::optional<std::pair<std::string, std::string>> paste_source_and_destination;
    bool paste_is_cut = false;

public:

    struct PasteRequest {
        std::string source;
        std::string destination;
        bool is_cut;
    };


    State();

    void freeEntries() {
        dir_entries.clear();
    }

    ~State() {
        freeEntries();
    }

    /** Reads the directory and replaces the entries. Does file operations, so it must not run on the LVGL task. */
    bool setEntriesForPath(const std::string& path);

    /** Whether the entry of the current directory is an executable. Determined by setEntriesForPath(). */
    bool isExecutable(const std::string& name) const {
        bool result = false;
        mutex.withLock([&] { result = std::ranges::find(executable_names, name) != executable_names.end(); });
        return result;
    }

    /** Records that a queued navigation finished, with the arguments for View::update() */
    void pushCompletedNavigation(bool first_page, bool select_first) {
        mutex.withLock([&] { completed_navigations.emplace_back(first_page, select_first); });
    }

    /** Takes the oldest finished navigation as {first_page, select_first}, or nullopt when there is none */
    std::optional<std::pair<bool, bool>> takeCompletedNavigation() {
        std::optional<std::pair<bool, bool>> result;
        mutex.withLock([&] {
            if (!completed_navigations.empty()) {
                result = completed_navigations.front();
                completed_navigations.erase(completed_navigations.begin());
            }
        });
        return result;
    }

    template <std::invocable<const std::vector<dirent> &> Func>
    void withEntries(Func&& onEntries) const {
        mutex.withLock([&] {
            std::invoke(std::forward<Func>(onEntries), dir_entries);
        });
    }

    void setSelectedChildEntry(const std::string& newFile) {
        mutex.withLock([&] {
            selected_child_entry = newFile;
            action = ActionNone;
        });
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

    std::string getSelectedChildPath() const;

    PendingAction getPendingAction() const {
        PendingAction result = ActionNone;
        mutex.withLock([&] { result = action; });
        return result;
    }

    void setPendingAction(PendingAction newAction) {
        mutex.withLock([&] { action = newAction; });
    }

    std::string getPendingPasteDst() const {
        std::string result;
        mutex.withLock([&] { result = pending_paste_dst; });
        return result;
    }

    void setPendingPasteDst(const std::string& dst) {
        mutex.withLock([&] { pending_paste_dst = dst; });
    }

    /** The source and mode of the paste that waits for confirmation, which can differ from the clipboard by then */
    std::string getPendingPasteSource() const {
        std::string result;
        mutex.withLock([&] { result = pending_paste_src; });
        return result;
    }

    bool getPendingPasteIsCut() const {
        bool result = false;
        mutex.withLock([&] { result = pending_paste_is_cut; });
        return result;
    }

    void setPendingPasteSource(const std::string& src, bool is_cut) {
        mutex.withLock([&] {
            pending_paste_src = src;
            pending_paste_is_cut = is_cut;
        });
    }

    /** Snapshot dst's stat at confirm-dialog time, so it can be revalidated right before the destructive delete. */
    void setPendingPasteDstStat(const struct stat& st) {
        mutex.withLock([&] {
            pending_paste_dst_stat = st;
            pending_paste_dst_stat_valid = true;
        });
    }

    void clearPendingPasteDstStat() {
        mutex.withLock([&] { pending_paste_dst_stat_valid = false; });
    }

    /** True if dst had no prior snapshot (nothing to overwrite) or `st` still matches it. */
    bool pendingPasteDstMatches(const struct stat& st) const {
        bool result = false;
        mutex.withLock([&] {
            result = !pending_paste_dst_stat_valid ||
                (pending_paste_dst_stat.st_dev == st.st_dev &&
                 pending_paste_dst_stat.st_ino == st.st_ino &&
                 pending_paste_dst_stat.st_size == st.st_size &&
                 pending_paste_dst_stat.st_mtime == st.st_mtime);
        });
        return result;
    }

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

    /** Hands a paste to the app task, which does the file system work. A newer request replaces one that wasn't taken yet. */
    void setPasteRequest(const PasteRequest& request) {
        mutex.withLock([&] {
            paste_source_and_destination = { request.source, request.destination };
            paste_is_cut = request.is_cut;
        });
    }

    /** Takes the pending paste request, or nullopt when there is none. */
    std::optional<PasteRequest> takePasteRequest() {
        std::optional<PasteRequest> result;
        mutex.withLock([&] {
            if (paste_source_and_destination.has_value()) {
                result = PasteRequest { paste_source_and_destination->first, paste_source_and_destination->second, paste_is_cut };
                paste_source_and_destination.reset();
            }
        });
        return result;
    }

    /** Clears the clipboard only while it still holds @a path as a cut, so a newer entry stays */
    void clearClipboardIfCut(const std::string& path) {
        mutex.withLock([&] {
            if (clipboard_active && clipboard_is_cut && clipboard_path == path) {
                clipboard_active = false;
                clipboard_path.clear();
                clipboard_is_cut = false;
            }
        });
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
