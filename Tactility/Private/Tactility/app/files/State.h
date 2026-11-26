#pragma once

#include <Tactility/RecursiveMutex.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

// Included last, as it makes std:: resolve to tt::std
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

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

public:

    struct PasteRequest {
        ::std::vector<::std::string> sources;
        ::std::string destination_folder;
        bool is_cut;
    };

private:

    RecursiveMutex mutex;
    ::std::string current_path;
    bool loaded = false;
    ::std::string selected_child_entry;
    PendingAction action = ActionNone;
    ::std::string pending_paste_dst;
    ::std::string pending_paste_src;
    bool pending_paste_is_cut = false;
    struct stat pending_paste_dst_stat {};
    bool pending_paste_dst_stat_valid = false;
    tt::std::vector<tt::std::string> selected_entries;
    tt::std::vector<tt::std::string> pending_delete_paths;
    tt::std::vector<tt::std::string> clipboard_paths;
    bool clipboard_is_cut = false;
    bool clipboard_active = false;
    ::std::optional<PasteRequest> paste_request;

public:

    State();

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

    /**
     * Sets the directory that is shown, after it was read. Another directory than before has no selection and no pending action.
     */
    void setCurrentPath(const ::std::string& path) {
        mutex.withLock([&] {
            if (path != current_path) {
                current_path = path;
                selected_child_entry = "";
                selected_entries.clear();
                action = ActionNone;
            }
            loaded = true;
        });
    }

    /** Whether a directory was read since the app started */
    bool isLoaded() const {
        bool result = false;
        mutex.withLock([&] { result = loaded; });
        return result;
    }

    void setSelectedChildEntry(const ::std::string& newFile) {
        mutex.withLock([&] {
            selected_child_entry = newFile;
            action = ActionNone;
        });
    }

    ::std::string getSelectedChildEntry() const {
        ::std::string result;
        mutex.withLock([&] { result = selected_child_entry; });
        return result;
    }

    ::std::string getCurrentPath() const {
        ::std::string result;
        mutex.withLock([&] { result = current_path; });
        return result;
    }

    ::std::string getSelectedChildPath() const;

    PendingAction getPendingAction() const {
        PendingAction result = ActionNone;
        mutex.withLock([&] { result = action; });
        return result;
    }

    void setPendingAction(PendingAction newAction) {
        mutex.withLock([&] { action = newAction; });
    }

    ::std::string getPendingPasteDst() const {
        ::std::string result;
        mutex.withLock([&] { result = pending_paste_dst; });
        return result;
    }

    void setPendingPasteDst(const ::std::string& dst) {
        mutex.withLock([&] { pending_paste_dst = dst; });
    }

    /** The source and mode of the paste that waits for confirmation, which can differ from the clipboard by then */
    ::std::string getPendingPasteSource() const {
        ::std::string result;
        mutex.withLock([&] { result = pending_paste_src; });
        return result;
    }

    bool getPendingPasteIsCut() const {
        bool result = false;
        mutex.withLock([&] { result = pending_paste_is_cut; });
        return result;
    }

    void setPendingPasteSource(const ::std::string& src, bool is_cut) {
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

    void setClipboard(const ::std::vector<::std::string>& paths, bool is_cut) {
        mutex.withLock([&] {
            clipboard_paths = toExt(paths);
            clipboard_is_cut = is_cut;
            clipboard_active = !paths.empty();
        });
    }

    bool hasClipboard() const {
        bool result = false;
        mutex.withLock([&] { result = clipboard_active; });
        return result;
    }

    /** Returns {paths, is_cut} atomically, or nullopt if clipboard is empty. */
    ::std::optional<::std::pair<::std::vector<::std::string>, bool>> getClipboard() const {
        ::std::optional<::std::pair<::std::vector<::std::string>, bool>> result;
        mutex.withLock([&] {
            if (clipboard_active) {
                result = { toStd(clipboard_paths), clipboard_is_cut };
            }
        });
        return result;
    }

    /** Hands a paste to the app task, which does the file system work. A newer request replaces one that wasn't taken yet. */
    void setPasteRequest(const PasteRequest& request) {
        mutex.withLock([&] { paste_request = request; });
    }

    /** Takes the pending paste request, or nullopt when there is none. */
    ::std::optional<PasteRequest> takePasteRequest() {
        ::std::optional<PasteRequest> result;
        mutex.withLock([&] {
            result = ::std::move(paste_request);
            paste_request.reset();
        });
        return result;
    }

    /** Removes @a path from the clipboard while it holds a cut, so a newer copy stays */
    void removeFromClipboardIfCut(const ::std::string& path) {
        mutex.withLock([&] {
            if (clipboard_active && clipboard_is_cut) {
                const auto index = indexOf(clipboard_paths, path);
                if (index >= 0) {
                    clipboard_paths.erase(clipboard_paths.begin() + index);
                }
                if (clipboard_paths.empty()) {
                    clipboard_active = false;
                    clipboard_is_cut = false;
                }
            }
        });
    }

    void clearClipboard() {
        mutex.withLock([&] {
            clipboard_active = false;
            clipboard_paths.clear();
            clipboard_is_cut = false;
        });
    }

    /** The entries of the current directory that actions apply to. Cleared when the directory changes. */
    ::std::vector<::std::string> getSelectedEntries() const {
        ::std::vector<::std::string> result;
        mutex.withLock([&] { result = toStd(selected_entries); });
        return result;
    }

    void selectOnlyEntry(const ::std::string& name) {
        mutex.withLock([&] {
            selected_entries.clear();
            selected_entries.push_back(toExt(name));
        });
    }

    /** Adds the entry, or removes it when it was selected. Returns whether it is selected now. */
    bool toggleSelectedEntry(const ::std::string& name) {
        bool selected = false;
        mutex.withLock([&] {
            const auto index = indexOf(selected_entries, name);
            if (index >= 0) {
                selected_entries.erase(selected_entries.begin() + index);
            } else {
                selected_entries.push_back(toExt(name));
                selected = true;
            }
        });
        return selected;
    }

    bool isSelectedEntry(const ::std::string& name) const {
        bool selected = false;
        mutex.withLock([&] { selected = indexOf(selected_entries, name) >= 0; });
        return selected;
    }

    void clearSelectedEntries() {
        mutex.withLock([&] { selected_entries.clear(); });
    }

    /** The paths that wait for the answer of the delete confirmation */
    ::std::vector<::std::string> getPendingDeletePaths() const {
        ::std::vector<::std::string> result;
        mutex.withLock([&] { result = toStd(pending_delete_paths); });
        return result;
    }

    void setPendingDeletePaths(const ::std::vector<::std::string>& paths) {
        mutex.withLock([&] { pending_delete_paths = toExt(paths); });
    }
};

}
