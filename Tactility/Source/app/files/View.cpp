#include <Tactility/app/files/SupportedFiles.h>
#include <Tactility/app/files/View.h>

#include <Tactility/LogMessages.h>
#include <Tactility/Logger.h>
#include <Tactility/StringUtils.h>
#include <Tactility/Tactility.h>
#include <Tactility/app/ElfApp.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/imageviewer/ImageViewer.h>
#include <Tactility/app/inputdialog/InputDialog.h>
#include <Tactility/app/notes/Notes.h>
#include <Tactility/file/File.h>
#include <Tactility/kernel/Platform.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <tactility/check.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include <Tactility/service/loader/Loader.h>
#endif

namespace tt::app::files {

static const auto LOGGER = Logger("Files");

// region Callbacks

static void dirEntryListScrollBeginCallback(lv_event_t* event) {
    auto* view = static_cast<files::View*>(lv_event_get_user_data(event));
    view->onDirEntryListScrollBegin();
}

static void onDirEntryPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    auto index = lv_obj_get_index(button);
    view->onDirEntryPressed(index);
}

static void onDirEntryLongPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    auto index = lv_obj_get_index(button);
    view->onDirEntryLongPressed(index);
}

static void onRenamePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onRenamePressed();
}

static void onDeletePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onDeletePressed();
}

static void onNavigateUpPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNavigateUpPressed();
}

static void onNewFilePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNewFilePressed();
}

static void onNewFolderPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onNewFolderPressed();
}

static void onCopyPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onCopyPressed();
}

static void onCutPressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onCutPressed();
}

static void onPastePressedCallback(lv_event_t* event) {
    auto* view = static_cast<View*>(lv_event_get_user_data(event));
    view->onPastePressed();
}

// endregion

// region File helpers

static bool copyFileContents(const std::string& src, const std::string& dst) {
    auto src_lock = file::getLock(src);
    auto dst_lock = file::getLock(dst);
    const bool same_lock = (src_lock.get() == dst_lock.get());

    auto unlock_all = [&] {
        if (!same_lock) dst_lock->unlock();
        src_lock->unlock();
    };

    src_lock->lock();
    if (!same_lock) dst_lock->lock();

    FILE* in = fopen(src.c_str(), "rb");
    if (in == nullptr) {
        unlock_all();
        return false;
    }
    FILE* out = fopen(dst.c_str(), "wb");
    if (out == nullptr) {
        fclose(in);
        unlock_all();
        return false;
    }
    uint8_t buf[512];
    bool success = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            success = false;
            break;
        }
    }
    if (ferror(in)) {
        success = false;
    }
    fclose(in);
    if (fclose(out) != 0) {
        success = false;
    }
    if (!success) {
        remove(dst.c_str());
    }
    unlock_all();
    return success;
}

static bool copyRecursive(const std::string& src, const std::string& dst) {
    if (file::isDirectory(src)) {
        if (!file::findOrCreateDirectory(dst, 0755)) {
            return false;
        }

        // Process one entry at a time: release the device lock between iterations
        // so other SPI bus users aren't starved, and stop immediately on failure.
        auto lock = file::getLock(src);
        lock->lock();
        DIR* dir = opendir(src.c_str());
        if (!dir) {
            lock->unlock();
            file::deleteRecursively(dst);
            return false;
        }

        bool success = true;
        while (success) {
            struct dirent* entry = readdir(dir);
            if (!entry) break;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            std::string name = entry->d_name; // copy before releasing lock
            lock->unlock();

            success = copyRecursive(file::getChildPath(src, name), file::getChildPath(dst, name));

            lock->lock();
        }
        closedir(dir);
        lock->unlock();

        if (!success) {
            file::deleteRecursively(dst);
        }
        return success;
    } else {
        return copyFileContents(src, dst);
    }
}

// endregion

void View::viewFile(const std::string& path, const std::string& filename) {
    std::string file_path = path + "/" + filename;

    // For PC we need to make the path relative to the current work directory,
    // because that's how LVGL maps its 'drive letter' to the file system.
    std::string processed_filepath;
    if (kernel::getPlatform() == kernel::PlatformSimulator) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            LOGGER.error("Failed to get current working directory");
            return;
        }
        if (!file_path.starts_with(cwd)) {
            LOGGER.error("Can only work with files in working directory {}", cwd);
            return;
        }
        processed_filepath = file_path.substr(strlen(cwd));
    } else {
        processed_filepath = file_path;
    }

    LOGGER.info("Clicked {}", file_path);

    if (isSupportedAppFile(filename)) {
#ifdef ESP_PLATFORM
        // install(filename);
        auto message = std::format("Do you want to install {}?", filename);
        installAppPath = processed_filepath;
        auto choices = std::vector {"Yes", "No"};
        installAppLaunchId = alertdialog::start("Install?", message, choices);
#endif
    } else if (isSupportedImageFile(filename)) {
        imageviewer::start(processed_filepath);
    } else if (isSupportedTextFile(filename)) {
        if (kernel::getPlatform() == kernel::PlatformEsp) {
            notes::start(processed_filepath);
        } else {
            // Remove forward slash, because we need a relative path
            notes::start(processed_filepath.substr(1));
        }
    } else {
        LOGGER.warn("Opening files of this type is not supported");
    }

    onNavigate();
}

bool View::resolveDirentFromListIndex(int32_t list_index, dirent& out_entry) {
    const bool is_root = (state->getCurrentPath() == "/");
    const bool has_back = (!is_root && current_start_index > 0);

    if (has_back && list_index == 0) {
        return false; // Back button
    }

    const size_t adjusted_index =
        current_start_index + static_cast<size_t>(list_index) - (has_back ? 1 : 0);

    return state->getDirent(static_cast<uint32_t>(adjusted_index), out_entry);
}

void View::onDirEntryPressed(uint32_t index) {
    dirent dir_entry;
    if (!resolveDirentFromListIndex(static_cast<int32_t>(index), dir_entry)) {
        return;
    }

    LOGGER.info("Pressed {} {}", dir_entry.d_name, dir_entry.d_type);
    state->setSelectedChildEntry(dir_entry.d_name);

    using namespace tt::file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR:
            state->setEntriesForChildPath(dir_entry.d_name);
            onNavigate();
            update();
            break;

        case TT_DT_LNK:
            LOGGER.warn("opening links is not supported");
            break;

        default:
            viewFile(state->getCurrentPath(), dir_entry.d_name);
            onNavigate();
            break;
    }
}

void View::onDirEntryLongPressed(int32_t index) {
    dirent dir_entry;
    if (!resolveDirentFromListIndex(index, dir_entry)) {
        return;
    }

    LOGGER.info("Pressed {} {}", dir_entry.d_name, dir_entry.d_type);
    state->setSelectedChildEntry(dir_entry.d_name);

    using namespace file;
    switch (dir_entry.d_type) {
        case TT_DT_DIR:
        case TT_DT_CHR:
            showActionsForDirectory();
            break;

        case TT_DT_LNK:
            LOGGER.warn("Opening links is not supported");
            break;

        default:
            showActionsForFile();
            break;
    }
}

void View::createDirEntryWidget(lv_obj_t* list, dirent& dir_entry) {
    check(list);
    const char* symbol;
    if (dir_entry.d_type == file::TT_DT_DIR || dir_entry.d_type == file::TT_DT_CHR) {
        symbol = LV_SYMBOL_DIRECTORY;
    } else if (isSupportedImageFile(dir_entry.d_name)) {
        symbol = LV_SYMBOL_IMAGE;
    } else if (dir_entry.d_type == file::TT_DT_LNK) {
        symbol = LV_SYMBOL_LOOP;
    } else {
        symbol = LV_SYMBOL_FILE;
    }

    // Get file size for regular files
    std::string label_text = dir_entry.d_name;
    if (dir_entry.d_type == file::TT_DT_REG) {
        std::string file_path = file::getChildPath(state->getCurrentPath(), dir_entry.d_name);
        struct stat st;
        if (stat(file_path.c_str(), &st) == 0) {
            // Format file size in human-readable format
            const char* size_suffix;
            double size;
            if (st.st_size < 1024) {
                size = st.st_size;
                size_suffix = " B";
            } else if (st.st_size < 1024 * 1024) {
                size = st.st_size / 1024.0;
                size_suffix = " KB";
            } else {
                size = st.st_size / (1024.0 * 1024.0);
                size_suffix = " MB";
            }

            char size_str[32];
            if (st.st_size < 1024) {
                snprintf(size_str, sizeof(size_str), " (%d%s)", (int)size, size_suffix);
            } else {
                snprintf(size_str, sizeof(size_str), " (%.1f%s)", size, size_suffix);
            }
            label_text += size_str;
        }
    }

    lv_obj_t* button = lv_list_add_button(list, symbol, label_text.c_str());
    lv_obj_add_event_cb(button, &onDirEntryPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(button, &onDirEntryLongPressedCallback, LV_EVENT_LONG_PRESSED, this);
}

void View::onNavigateUpPressed() {
    if (state->getCurrentPath() != "/") {
        LOGGER.info("Navigating upwards");
        std::string new_absolute_path;
        if (string::getPathParent(state->getCurrentPath(), new_absolute_path)) {
            state->setEntriesForPath(new_absolute_path);
        }
        onNavigate();
        update();
    }
}

void View::onRenamePressed() {
    std::string entry_name = state->getSelectedChildEntry();
    LOGGER.info("Pending rename {}", entry_name);
    state->setPendingAction(State::ActionRename);
    inputdialog::start("Rename", "", entry_name);
}

void View::onDeletePressed() {
    std::string file_path = state->getSelectedChildPath();
    LOGGER.info("Pending delete {}", file_path);
    state->setPendingAction(State::ActionDelete);
    std::string message = "Do you want to delete this?\n" + file_path;
    const std::vector<std::string> choices = {"Yes", "No"};
    alertdialog::start("Are you sure?", message, choices);
}

void View::onNewFilePressed() {
    LOGGER.info("Creating new file");
    state->setPendingAction(State::ActionCreateFile);
    inputdialog::start("New File", "Enter filename:", "");
}

void View::onNewFolderPressed() {
    LOGGER.info("Creating new folder");
    state->setPendingAction(State::ActionCreateFolder);
    inputdialog::start("New Folder", "Enter folder name:", "");
}

void View::showActions() {
    lv_obj_clean(action_list);

    auto* copy_button = lv_list_add_button(action_list, LV_SYMBOL_COPY, "Copy");
    lv_obj_add_event_cb(copy_button, onCopyPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* cut_button = lv_list_add_button(action_list, LV_SYMBOL_CUT, "Cut");
    lv_obj_add_event_cb(cut_button, onCutPressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* rename_button = lv_list_add_button(action_list, LV_SYMBOL_EDIT, "Rename");
    lv_obj_add_event_cb(rename_button, onRenamePressedCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* delete_button = lv_list_add_button(action_list, LV_SYMBOL_TRASH, "Delete");
    lv_obj_add_event_cb(delete_button, onDeletePressedCallback, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_remove_flag(action_list, LV_OBJ_FLAG_HIDDEN);
}

void View::showActionsForDirectory() { showActions(); }
void View::showActionsForFile() { showActions(); }

void View::update(size_t start_index) {
    const bool is_root = (state->getCurrentPath() == "/");

    auto scoped_lockable = lvgl::getSyncLock()->asScopedLock();
    if (!scoped_lockable.lock(lvgl::defaultLockTime)) {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "lvgl");
        return;
    }

    lv_obj_clean(dir_entry_list);

    current_start_index = start_index;

    state->withEntries([this, is_root](const std::vector<dirent>& entries) {
        size_t total_entries = entries.size();
        if (current_start_index >= total_entries) {
            current_start_index = (total_entries > MAX_BATCH)
                ? (total_entries - MAX_BATCH)
                : 0;
        }
        size_t count = 0;

        if (!is_root && current_start_index > 0) {
            auto* back_btn = lv_list_add_btn(dir_entry_list, LV_SYMBOL_LEFT, "Back");
            lv_obj_add_event_cb(back_btn, [](lv_event_t* event) {
                auto* view = static_cast<View*>(lv_event_get_user_data(event));
                size_t new_index = (view->current_start_index >= view->MAX_BATCH) ? 
                                    view->current_start_index - view->MAX_BATCH : 0;
                view->update(new_index); }, LV_EVENT_SHORT_CLICKED, this);
        }

        for (size_t i = current_start_index; i < total_entries; ++i) {
            auto entry = entries[i];

            createDirEntryWidget(dir_entry_list, entry);
            count++;

            if (count >= MAX_BATCH) {
                break;
            }
        }

        last_loaded_index = std::min(current_start_index + count, total_entries);

        if (!is_root && last_loaded_index < total_entries) {
            if (total_entries > current_start_index &&
+                (total_entries - current_start_index) > MAX_BATCH) {
                auto* next_btn = lv_list_add_btn(dir_entry_list, LV_SYMBOL_RIGHT, "Next");
                lv_obj_add_event_cb(next_btn, [](lv_event_t* event) {
                    auto* view = static_cast<View*>(lv_event_get_user_data(event));
                    view->update(view->last_loaded_index); }, LV_EVENT_SHORT_CLICKED, this);
            }
        } else {
            last_loaded_index = total_entries;
        }
    });

    if (is_root) {
        lv_obj_add_flag(lv_obj_get_parent(navigate_up_button), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(lv_obj_get_parent(navigate_up_button), LV_OBJ_FLAG_HIDDEN);
    }

    if (state->hasClipboard() && !is_root) {
        lv_obj_remove_flag(lv_obj_get_parent(paste_button), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lv_obj_get_parent(paste_button), LV_OBJ_FLAG_HIDDEN);
    }
}

void View::init(const AppContext& appContext, lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl::toolbar_create(parent, appContext);
    navigate_up_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_UP, &onNavigateUpPressedCallback, this);
    new_file_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_FILE, &onNewFilePressedCallback, this);
    new_folder_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_DIRECTORY, &onNewFolderPressedCallback, this);
    paste_button = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_PASTE, &onPastePressedCallback, this);
    lv_obj_add_flag(lv_obj_get_parent(paste_button), LV_OBJ_FLAG_HIDDEN);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);

    dir_entry_list = lv_list_create(wrapper);
    lv_obj_set_height(dir_entry_list, LV_PCT(100));
    lv_obj_set_flex_grow(dir_entry_list, 1);

    lv_obj_add_event_cb(dir_entry_list, dirEntryListScrollBeginCallback, LV_EVENT_SCROLL_BEGIN, this);

    action_list = lv_list_create(wrapper);
    lv_obj_set_height(action_list, LV_PCT(100));
    lv_obj_set_flex_grow(action_list, 1);
    lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);

    update();
}

void View::onDirEntryListScrollBegin() {
    auto scoped_lockable = lvgl::getSyncLock()->asScopedLock();
    if (scoped_lockable.lock(lvgl::defaultLockTime)) {
        lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);
    }
}

void View::onNavigate() {
    auto scoped_lockable = lvgl::getSyncLock()->asScopedLock();
    if (scoped_lockable.lock(lvgl::defaultLockTime)) {
        lv_obj_add_flag(action_list, LV_OBJ_FLAG_HIDDEN);
    }
}

void View::onResult(LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle) {
    if (result != Result::Ok || bundle == nullptr) {
        return;
    }

    if (
        launchId == installAppLaunchId &&
        result == Result::Ok &&
        alertdialog::getResultIndex(*bundle) == 0
    ) {
        install(installAppPath);
        return;
    }

    std::string filepath = state->getSelectedChildPath();
    LOGGER.info("Result for {}", filepath);

    switch (state->getPendingAction()) {
        case State::ActionDelete: {
            if (alertdialog::getResultIndex(*bundle) == 0) {
                if (file::isDirectory(filepath)) {
                    if (!file::deleteRecursively(filepath)) {
                        LOGGER.warn("Failed to delete {}", filepath);
                    }
                } else if (file::isFile(filepath)) {
                    auto lock = file::getLock(filepath);
                    lock->lock();
                    if (remove(filepath.c_str()) <= 0) {
                        LOGGER.warn("Failed to delete {}", filepath);
                    }
                    lock->unlock();
                }

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        case State::ActionRename: {
            auto new_name = inputdialog::getResult(*bundle);
            if (!new_name.empty() && new_name != state->getSelectedChildEntry()) {
                auto lock = file::getLock(filepath);
                lock->lock();
                std::string rename_to = file::getChildPath(state->getCurrentPath(), new_name);
                struct stat st;
                if (stat(rename_to.c_str(), &st) == 0) {
                    LOGGER.warn("Rename: destination already exists: \"{}\"", rename_to);
                    lock->unlock();
                    state->setPendingAction(State::ActionNone);
                    alertdialog::start("Rename failed", "\"" + new_name + "\" already exists.");
                    break;
                }
                if (rename(filepath.c_str(), rename_to.c_str()) == 0) {
                    LOGGER.info("Renamed \"{}\" to \"{}\"", filepath, rename_to);
                } else {
                    LOGGER.error("Failed to rename \"{}\" to \"{}\"", filepath, rename_to);
                }
                lock->unlock();

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        case State::ActionCreateFile: {
            auto filename = inputdialog::getResult(*bundle);
            if (!filename.empty()) {
                std::string new_file_path = file::getChildPath(state->getCurrentPath(), filename);

                auto lock = file::getLock(new_file_path);
                lock->lock();

                struct stat st;
                if (stat(new_file_path.c_str(), &st) == 0) {
                    LOGGER.warn("File already exists: \"{}\"", new_file_path);
                    lock->unlock();
                    break;
                }

                FILE* new_file = fopen(new_file_path.c_str(), "w");
                if (new_file) {
                    fclose(new_file);
                    LOGGER.info("Created file \"{}\"", new_file_path);
                } else {
                    LOGGER.error("Failed to create file \"{}\"", new_file_path);
                }
                lock->unlock();

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        case State::ActionCreateFolder: {
            auto foldername = inputdialog::getResult(*bundle);
            if (!foldername.empty()) {
                std::string new_folder_path = file::getChildPath(state->getCurrentPath(), foldername);

                auto lock = file::getLock(new_folder_path);
                lock->lock();

                struct stat st;
                if (stat(new_folder_path.c_str(), &st) == 0) {
                    LOGGER.warn("Folder already exists: \"{}\"", new_folder_path);
                    lock->unlock();
                    break;
                }

                if (mkdir(new_folder_path.c_str(), 0755) == 0) {
                    LOGGER.info("Created folder \"{}\"", new_folder_path);
                } else {
                    LOGGER.error("Failed to create folder \"{}\"", new_folder_path);
                }
                lock->unlock();

                state->setEntriesForPath(state->getCurrentPath());
                update();
            }
            break;
        }
        case State::ActionPaste: {
            if (alertdialog::getResultIndex(*bundle) == 0) {
                auto clipboard = state->getClipboard();
                if (clipboard.has_value()) {
                    std::string dst = state->getPendingPasteDst();
                    // Trade-off: dst is removed before the copy attempt. If doPaste
                    // subsequently fails (e.g. source read error, out of space), the
                    // original dst data is unrecoverable. Acceptable for an embedded
                    // file manager; a safer approach would rename dst to a temp path
                    // first and roll back on failure.
                    if (file::deleteRecursively(dst)) {
                        doPaste(clipboard->first, clipboard->second, dst);
                    } else {
                        LOGGER.error("Overwrite: failed to remove existing destination: \"{}\"", dst);
                        state->setPendingAction(State::ActionNone);
                        alertdialog::start(
                            "Overwrite failed",
                            "Could not remove \"" + file::getLastPathSegment(dst) + "\" before overwriting."
                        );
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void View::onCopyPressed() {
    std::string path = state->getSelectedChildPath();
    state->setClipboard(path, false);
    LOGGER.info("Copied to clipboard: {}", path);
    onNavigate();
    update();
}

void View::onCutPressed() {
    std::string path = state->getSelectedChildPath();
    state->setClipboard(path, true);
    LOGGER.info("Cut to clipboard: {}", path);
    onNavigate();
    update();
}

void View::onPastePressed() {
    auto clipboard = state->getClipboard();
    if (!clipboard.has_value()) return;

    std::string src = clipboard->first;
    bool is_cut = clipboard->second;
    std::string entry_name = file::getLastPathSegment(src);
    std::string dst = file::getChildPath(state->getCurrentPath(), entry_name);

    // Note: getLock(src) guards the source path; the existence check below is
    // against dst, so there is a TOCTOU gap — another writer could create dst
    // between this check and the write inside doPaste.  Acceptable on a
    // single-user embedded device; locking dst instead would be more correct.
    if (src == dst) {
        LOGGER.info("Paste: source and destination are the same path, skipping");
        return;
    }
    auto lock = file::getLock(src);
    lock->lock();

    struct stat st;
    bool dst_exists = (stat(dst.c_str(), &st) == 0);
    lock->unlock();

    if (dst_exists) {
        state->setPendingPasteDst(dst);
        state->setPendingAction(State::ActionPaste);
        const std::vector<std::string> choices = {"Overwrite", "Cancel"};
        alertdialog::start("File exists", "Overwrite \"" + entry_name + "\"?", choices);
        return;
    }

    doPaste(src, is_cut, dst);
}

void View::doPaste(const std::string& src, bool is_cut, const std::string& dst) {
    bool success = false;
    bool src_delete_failed = false;
    if (is_cut) {
        auto lock = file::getLock(src);
        lock->lock();
        success = (rename(src.c_str(), dst.c_str()) == 0);
        lock->unlock();
        if (!success) {
            // Fallback for cross-filesystem moves: copy then delete.
            // Only mark success if both halves succeed — if the source removal
            // fails we leave success=false so the clipboard is preserved and
            // the error is surfaced; the user must remove the source manually.
            if (copyRecursive(src, dst)) {
                if (file::deleteRecursively(src)) {
                    success = true;
                } else {
                    src_delete_failed = true;
                    LOGGER.error("Cut: copied \"{}\" to \"{}\" but failed to remove source — manual cleanup required", src, dst);
                }
            }
        }
    } else {
        success = copyRecursive(src, dst);
    }

    const std::string filename = file::getLastPathSegment(src);
    if (success) {
        LOGGER.info("{} \"{}\" to \"{}\"", is_cut ? "Moved" : "Copied", src, dst);
        if (is_cut) {
            state->clearClipboard();
        }
    } else if (src_delete_failed) {
        state->setPendingAction(State::ActionNone); // prevent re-trigger on dialog dismiss
        alertdialog::start("Move incomplete", "\"" + filename + "\" was copied but the original could not be removed.\nPlease delete it manually.");
    } else {
        LOGGER.error("Failed to {} \"{}\" to \"{}\"", is_cut ? "move" : "copy", src, dst);
        state->setPendingAction(State::ActionNone); // prevent re-trigger on dialog dismiss
        alertdialog::start(
            std::string("Failed to ") + (is_cut ? "move" : "copy"),
            "\"" + filename + "\" could not be " + (is_cut ? "moved." : "copied.")
        );
    }

    state->setEntriesForPath(state->getCurrentPath());
    update();
}

void View::deinit(const AppContext& appContext) {
    lv_obj_remove_event_cb(dir_entry_list, dirEntryListScrollBeginCallback);
}

} // namespace tt::app::files
